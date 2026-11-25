#include "RatRigActor.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

bool URatPoseableMeshComponent::PublishComponentPose(const TArray<FTransform>& Pose)
{
    const USkeletalMesh* Asset = Cast<USkeletalMesh>(GetSkinnedAsset());
    if (!Asset) return false;

    const FReferenceSkeleton& Ref = Asset->GetRefSkeleton();

    if (Pose.Num() != Ref.GetNum() || BoneSpaceTransforms.Num() != Pose.Num())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Rat: pose size mismatch: solved=%d, reference=%d, mesh=%d"),
            Pose.Num(), Ref.GetNum(), BoneSpaceTransforms.Num());
        return false;
    }

    for (int32 I = 0; I < Pose.Num(); ++I)
    {
        const int32 P = Ref.GetParentIndex(I);

        BoneSpaceTransforms[I] = P == INDEX_NONE
            ? Pose[I]
            : Pose[I].GetRelativeTransform(Pose[P]);

        BoneSpaceTransforms[I].NormalizeRotation();
    }

    MarkRefreshTransformDirty();
    RefreshBoneTransforms();
    MarkRenderDynamicDataDirty();

    return true;
}

ARatRigActor::ARatRigActor()
{
    PrimaryActorTick.bCanEverTick = true;

    RatMesh = CreateDefaultSubobject<URatPoseableMeshComponent>(TEXT("RatMesh"));
    SetRootComponent(RatMesh);

    RatMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    const TCHAR* Defaults[] = {
        TEXT("Hip_L"),
        TEXT("Hip_R"),
        TEXT("Tail")
    };

    for (const TCHAR* B : Defaults)
    {
        FRatAttachment A;
        A.Bone = FName(B);
        A.NewParent = TEXT("Hip");
        Attachments.Add(A);
    }
}

void ARatRigActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildRig();
}

void ARatRigActor::BeginPlay()
{
    RebuildRig();
    Super::BeginPlay();
}

void ARatRigActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (bRigReady)
    {
        EvaluatePose(DeltaSeconds);
    }
}

TArray<FName> ARatRigActor::GetChainBones(ERatChain Chain)
{
    TArray<FName> Out;

    switch (Chain)
    {
    case ERatChain::Spine:
        Out.Add(TEXT("Hip"));
        Out.Add(TEXT("Torso"));
        Out.Add(TEXT("Chest"));
        Out.Add(TEXT("Neck"));
        Out.Add(TEXT("Head"));
        break;

    case ERatChain::Tail:
        Out.Add(TEXT("Tail"));
        Out.Add(TEXT("Tail1"));
        Out.Add(TEXT("Tail2"));
        Out.Add(TEXT("Tail3"));
        Out.Add(TEXT("Tail4"));
        Out.Add(TEXT("Tail5"));
        Out.Add(TEXT("Tail6"));
        break;

    case ERatChain::LegL:
        Out.Add(TEXT("Hip_L"));
        Out.Add(TEXT("Thigh_L"));
        Out.Add(TEXT("Shin_L"));
        Out.Add(TEXT("Foot_L"));
        break;

    case ERatChain::LegR:
        Out.Add(TEXT("Hip_R"));
        Out.Add(TEXT("Thigh_R"));
        Out.Add(TEXT("Shin_R"));
        Out.Add(TEXT("Foot_R"));
        break;

    case ERatChain::ArmL:
        Out.Add(TEXT("Clav_L"));
        Out.Add(TEXT("Shoulder_L"));
        Out.Add(TEXT("Forearm_L"));
        Out.Add(TEXT("Hand_L"));
        break;

    case ERatChain::ArmR:
        Out.Add(TEXT("Clav_R"));
        Out.Add(TEXT("Shoulder_R"));
        Out.Add(TEXT("Forearm_R"));
        Out.Add(TEXT("Hand_R"));
        break;
    }

    return Out;
}

bool ARatRigActor::RebuildRig()
{
    bRigReady = false;

    Motions.Empty();
    ReleaseAfterReset.Empty();
    DirectBoneEvents.Empty();

    Names.Empty();
    Parents.Empty();
    RestLocal.Empty();
    RestComponent.Empty();

    USkeletalMesh* Asset = RatAsset;

    if (!Asset)
    {
        Asset = Cast<USkeletalMesh>(RatMesh->GetSkinnedAsset());
    }

    if (!Asset)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Rat: assign Rat Asset or Rat Mesh's Skeletal Mesh."));
        return false;
    }

    RatAsset = Asset;

    if (RatMesh->GetSkinnedAsset() != Asset)
    {
        RatMesh->SetSkinnedAssetAndUpdate(Asset, true);
    }

    const FReferenceSkeleton& Ref = RatAsset->GetRefSkeleton();
    const int32 Count = Ref.GetNum();

    if (Count == 0) return false;

    RestLocal = Ref.GetRefBonePose();

    Parents.SetNum(Count);
    Names.SetNum(Count);
    RestComponent.SetNum(Count);

    WorkingComponent.SetNum(Count);
    VisitState.SetNumZeroed(Count);

    for (int32 I = 0; I < Count; ++I)
    {
        Names[I] = Ref.GetBoneName(I);
        Parents[I] = Ref.GetParentIndex(I);

        RestComponent[I] = Parents[I] == INDEX_NONE
            ? RestLocal[I]
            : RestLocal[I] * RestComponent[Parents[I]];
    }

    for (const FRatAttachment& A : Attachments)
    {
        const int32 Bone = Names.IndexOfByKey(A.Bone);
        const int32 NewParent = Names.IndexOfByKey(A.NewParent);

        if (Bone == INDEX_NONE || NewParent == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Rat: attachment skipped (%s -> %s): bone not found"),
                *A.Bone.ToString(),
                *A.NewParent.ToString());

            continue;
        }

        bool bCycle = false;

        for (int32 P = NewParent; P != INDEX_NONE; P = Parents[P])
        {
            if (P == Bone)
            {
                bCycle = true;
                break;
            }
        }

        if (bCycle)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Rat: rejected cyclic attachment %s -> %s"),
                *A.Bone.ToString(),
                *A.NewParent.ToString());

            continue;
        }

        Parents[Bone] = NewParent;

        RestLocal[Bone] =
            RestComponent[Bone].GetRelativeTransform(RestComponent[NewParent]);

        UE_LOG(LogTemp, Log,
            TEXT("Rat: %s follows %s"),
            *Names[Bone].ToString(),
            *Names[NewParent].ToString());
    }

    bRigReady = true;

    EvaluatePose(0.f);

    return true;
}

// ------------------------------------------------------------
// Batch pose update
// ------------------------------------------------------------

bool ARatRigActor::ApplyBoneTargets(const TArray<FRatBoneTarget>& Targets)
{
    if (!bRigReady && !RebuildRig())
    {
        return false;
    }

    bool bSuccess = true;

    for (const FRatBoneTarget& Target : Targets)
    {
        const int32 Index = Names.IndexOfByKey(Target.BoneName);

        if (Index == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Rat: batch target bone %s not found"),
                *Target.BoneName.ToString());

            bSuccess = false;
            continue;
        }

        const FQuat Offset = MakeOffset(
            Target.Horizontal,
            Target.Vertical,
            Target.Roll
        );

        const FQuat JointLocal = ToJointLocal(Index, Offset);

        SetMotion(Index, JointLocal, 0.f, false);
    }

    EvaluatePose(0.f);

    return bSuccess;
}

// ------------------------------------------------------------
// Rotation helpers
// ------------------------------------------------------------

FQuat ARatRigActor::MakeOffset(float Horizontal, float Vertical, float Roll) const
{
    FVector H = HorizontalAxis.GetSafeNormal();
    FVector V = VerticalAxis.GetSafeNormal();
    FVector R = RollAxis.GetSafeNormal();

    if (H.IsNearlyZero()) H = FVector::UpVector;
    if (V.IsNearlyZero()) V = FVector::RightVector;
    if (R.IsNearlyZero()) R = FVector::ForwardVector;

    return (
        FQuat(V, FMath::DegreesToRadians(static_cast<double>(Vertical))) *
        FQuat(H, FMath::DegreesToRadians(static_cast<double>(Horizontal))) *
        FQuat(R, FMath::DegreesToRadians(static_cast<double>(Roll)))
    ).GetNormalized();
}

FQuat ARatRigActor::ToJointLocal(int32 Index, const FQuat& ComponentOffset) const
{
    const FQuat R = RestComponent[Index].GetRotation();

    return (
        R.Inverse() * ComponentOffset * R
    ).GetNormalized();
}

void ARatRigActor::SetMotion(int32 Index, FQuat Target, float Duration, bool Independent)
{
    ReleaseAfterReset.Remove(Index);

    const FQuat ComponentQ = WorkingComponent[Index].GetRotation();

    const int32 Parent = Parents[Index];

    const FQuat ParentQ = Parent == INDEX_NONE
        ? FQuat::Identity
        : WorkingComponent[Parent].GetRotation();

    FRatBoneMotion& M = Motions.FindOrAdd(Index);

    M.Start = Independent
        ? ComponentQ * RestComponent[Index].GetRotation().Inverse()
        : RestLocal[Index].GetRotation().Inverse() * ParentQ.Inverse() * ComponentQ;

    M.Start.Normalize();

    M.Current = M.Start;
    M.Target = Target;
    M.Duration = FMath::Max(0.f, Duration);
    M.Elapsed = 0.f;
    M.bIndependent = Independent;
}

// ------------------------------------------------------------
// Individual bone animation
// ------------------------------------------------------------

bool ARatRigActor::AnimateBone(
    FName BoneName,
    float HorizontalDegrees,
    float VerticalDegrees,
    float RollDegrees,
    float Duration,
    bool Independent)
{
    if (!bRigReady && !RebuildRig()) return false;

    const int32 I = Names.IndexOfByKey(BoneName);

    if (I == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Rat: bone %s not found"),
            *BoneName.ToString());

        return false;
    }

    FQuat Target = MakeOffset(
        HorizontalDegrees,
        VerticalDegrees,
        RollDegrees
    );

    if (!Independent)
    {
        Target = ToJointLocal(I, Target);
    }

    DirectBoneEvents.Add(I);

    SetMotion(I, Target, Duration, Independent);

    EvaluatePose(0.f);

    return true;
}

bool ARatRigActor::AnimateBoneRotator(
    FName BoneName,
    FRotator RotationDegrees,
    float Duration,
    bool Independent)
{
    if (!bRigReady && !RebuildRig()) return false;

    const int32 I = Names.IndexOfByKey(BoneName);

    if (I == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("Rat: bone %s not found"),
            *BoneName.ToString());

        return false;
    }

    FQuat Target = RotationDegrees.Quaternion().GetNormalized();

    if (Independent)
    {
        const FQuat RestQ = RestComponent[I].GetRotation();
        Target = (RestQ * Target * RestQ.Inverse()).GetNormalized();
    }

    DirectBoneEvents.Add(I);

    SetMotion(I, Target, Duration, Independent);

    EvaluatePose(0.f);

    return true;
}

// ------------------------------------------------------------
// Chain animation
// ------------------------------------------------------------

bool ARatRigActor::AnimateChain(
    ERatChain Chain,
    FName StartBone,
    float HorizontalDegrees,
    float VerticalDegrees,
    float HorizontalStepDegrees,
    float VerticalStepDegrees,
    float Duration,
    float RollDegrees,
    float RollStepDegrees)
{
    if (!bRigReady && !RebuildRig()) return false;

    const TArray<FName> Bones = GetChainBones(Chain);

    int32 StartIdx = 0;

    if (!StartBone.IsNone())
    {
        StartIdx = Bones.IndexOfByKey(StartBone);

        if (StartIdx == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Rat: %s is not part of the selected chain"),
                *StartBone.ToString());

            return false;
        }
    }

    for (int32 K = StartIdx; K < Bones.Num(); ++K)
    {
        if (!Names.Contains(Bones[K]))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("Rat: missing chain bone %s"),
                *Bones[K].ToString());

            return false;
        }
    }

    for (int32 K = StartIdx; K < Bones.Num(); ++K)
    {
        const int32 I = Names.IndexOfByKey(Bones[K]);

        if (DirectBoneEvents.Contains(I)) continue;

        const float Step = static_cast<float>(K - StartIdx);

        const FQuat Offset = MakeOffset(
            HorizontalDegrees + Step * HorizontalStepDegrees,
            VerticalDegrees + Step * VerticalStepDegrees,
            RollDegrees + Step * RollStepDegrees
        );

        SetMotion(I, ToJointLocal(I, Offset), Duration, false);
    }

    EvaluatePose(0.f);

    return true;
}

// ------------------------------------------------------------
// Convenience animation functions
// ------------------------------------------------------------

bool ARatRigActor::AnimateHead(float H, float V, float Roll, float Duration)
{
    return AnimateBone(TEXT("Head"), H, V, Roll, Duration, false);
}

bool ARatRigActor::AnimateNeck(float H, float V, float Roll, float Duration)
{
    return AnimateBone(TEXT("Neck"), H, V, Roll, Duration, false);
}

bool ARatRigActor::AnimateHips(float H, float V, float Roll, float Duration)
{
    return AnimateBone(TEXT("Hip"), H, V, Roll, Duration, false);
}

bool ARatRigActor::AnimateLegJoint(
    bool bLeft,
    ERatLegJoint Joint,
    float H,
    float V,
    float Roll,
    float Duration)
{
    FName BoneName = NAME_None;

    switch (Joint)
    {
    case ERatLegJoint::Hip:
        BoneName = bLeft ? TEXT("Hip_L") : TEXT("Hip_R");
        break;

    case ERatLegJoint::Thigh:
        BoneName = bLeft ? TEXT("Thigh_L") : TEXT("Thigh_R");
        break;

    case ERatLegJoint::Shin:
        BoneName = bLeft ? TEXT("Shin_L") : TEXT("Shin_R");
        break;

    case ERatLegJoint::Foot:
        BoneName = bLeft ? TEXT("Foot_L") : TEXT("Foot_R");
        break;
    }

    return AnimateBone(BoneName, H, V, Roll, Duration, false);
}

bool ARatRigActor::AnimateArmJoint(
    bool bLeft,
    ERatArmJoint Joint,
    float H,
    float V,
    float Roll,
    float Duration)
{
    FName BoneName = NAME_None;

    switch (Joint)
    {
    case ERatArmJoint::Clavicle:
        BoneName = bLeft ? TEXT("Clav_L") : TEXT("Clav_R");
        break;

    case ERatArmJoint::Shoulder:
        BoneName = bLeft ? TEXT("Shoulder_L") : TEXT("Shoulder_R");
        break;

    case ERatArmJoint::Forearm:
        BoneName = bLeft ? TEXT("Forearm_L") : TEXT("Forearm_R");
        break;

    case ERatArmJoint::Hand:
        BoneName = bLeft ? TEXT("Hand_L") : TEXT("Hand_R");
        break;
    }

    return AnimateBone(BoneName, H, V, Roll, Duration, false);
}

bool ARatRigActor::AnimateLeg(
    bool bLeft,
    float H,
    float V,
    float HStep,
    float VStep,
    float Duration,
    float RollDegrees,
    float RollStepDegrees,
    bool bFront)
{
    const ERatChain Chain = bFront
        ? (bLeft ? ERatChain::ArmL : ERatChain::ArmR)
        : (bLeft ? ERatChain::LegL : ERatChain::LegR);

    const FName StartBone = bFront
        ? (bLeft ? FName(TEXT("Shoulder_L")) : FName(TEXT("Shoulder_R")))
        : (bLeft ? FName(TEXT("Thigh_L")) : FName(TEXT("Thigh_R")));

    return AnimateChain(
        Chain,
        StartBone,
        H,
        V,
        HStep,
        VStep,
        Duration,
        RollDegrees,
        RollStepDegrees
    );
}

bool ARatRigActor::AnimateArm(
    bool bLeft,
    float H,
    float V,
    float HStep,
    float VStep,
    float Duration,
    float RollDegrees,
    float RollStepDegrees)
{
    return AnimateChain(
        bLeft ? ERatChain::ArmL : ERatChain::ArmR,
        bLeft ? FName(TEXT("Shoulder_L")) : FName(TEXT("Shoulder_R")),
        H,
        V,
        HStep,
        VStep,
        Duration,
        RollDegrees,
        RollStepDegrees
    );
}

bool ARatRigActor::AnimateTail(
    float H,
    float V,
    float HStep,
    float VStep,
    float Duration,
    FName StartBone,
    float RollDegrees,
    float RollStepDegrees)
{
    return AnimateChain(
        ERatChain::Tail,
        StartBone,
        H,
        V,
        HStep,
        VStep,
        Duration,
        RollDegrees,
        RollStepDegrees
    );
}

// ------------------------------------------------------------
// Reset functions
// ------------------------------------------------------------

bool ARatRigActor::ResetBone(FName BoneName, float Duration)
{
    if (!bRigReady) return false;

    const int32 I = Names.IndexOfByKey(BoneName);

    if (I == INDEX_NONE) return false;

    SetMotion(I, FQuat::Identity, Duration, false);

    DirectBoneEvents.Add(I);
    ReleaseAfterReset.Add(I);

    EvaluatePose(0.f);

    return true;
}

bool ARatRigActor::ResetChain(ERatChain Chain, float Duration)
{
    if (!bRigReady) return false;

    bool bAny = false;

    for (const FName& B : GetChainBones(Chain))
    {
        const int32 I = Names.IndexOfByKey(B);

        if (I == INDEX_NONE || !Motions.Contains(I)) continue;

        SetMotion(I, FQuat::Identity, Duration, false);

        DirectBoneEvents.Add(I);
        ReleaseAfterReset.Add(I);

        bAny = true;
    }

    EvaluatePose(0.f);

    return bAny;
}

void ARatRigActor::ResetAll(float Duration)
{
    if (!bRigReady) return;

    TArray<int32> Keys;
    Motions.GetKeys(Keys);

    for (int32 I : Keys)
    {
        SetMotion(I, FQuat::Identity, Duration, false);

        DirectBoneEvents.Add(I);
        ReleaseAfterReset.Add(I);
    }

    EvaluatePose(0.f);
}

// ------------------------------------------------------------
// Pose solver
// ------------------------------------------------------------

void ARatRigActor::SolveBone(int32 Index)
{
    if (VisitState[Index] == 2) return;

    check(VisitState[Index] != 1);

    VisitState[Index] = 1;

    const int32 Parent = Parents[Index];

    if (Parent != INDEX_NONE)
    {
        SolveBone(Parent);
    }

    FTransform Local = RestLocal[Index];

    const FRatBoneMotion* M = Motions.Find(Index);

    if (M && !M->bIndependent)
    {
        Local.SetRotation(
            (Local.GetRotation() * M->Current).GetNormalized()
        );
    }

    WorkingComponent[Index] = Parent == INDEX_NONE
        ? Local
        : Local * WorkingComponent[Parent];

    if (M && M->bIndependent)
    {
        WorkingComponent[Index].SetRotation(
            (M->Current * RestComponent[Index].GetRotation()).GetNormalized()
        );
    }

    VisitState[Index] = 2;
}

void ARatRigActor::EvaluatePose(float DeltaSeconds)
{
    for (auto& Pair : Motions)
    {
        FRatBoneMotion& M = Pair.Value;

        M.Elapsed += FMath::Max(0.f, DeltaSeconds);

        const float T = M.Duration <= KINDA_SMALL_NUMBER
            ? 1.f
            : FMath::Clamp(M.Elapsed / M.Duration, 0.f, 1.f);

        const float Smooth = T * T * (3.f - 2.f * T);

        M.Current = FQuat::Slerp(
            M.Start,
            M.Target,
            Smooth
        ).GetNormalized();
    }

    for (int32 I = 0; I < VisitState.Num(); ++I)
    {
        VisitState[I] = 0;
    }

    for (int32 I = 0; I < Names.Num(); ++I)
    {
        SolveBone(I);
    }

    URatPoseableMeshComponent* PoseMesh =
        Cast<URatPoseableMeshComponent>(RatMesh);

    if (PoseMesh)
    {
        PoseMesh->PublishComponentPose(WorkingComponent);
    }

    for (auto It = ReleaseAfterReset.CreateIterator(); It; ++It)
    {
        const FRatBoneMotion* M = Motions.Find(*It);

        if (!M || M->Elapsed >= M->Duration)
        {
            DirectBoneEvents.Remove(*It);
            Motions.Remove(*It);
            It.RemoveCurrent();
        }
    }
}