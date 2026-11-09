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
        UE_LOG(LogTemp, Warning, TEXT("Rat: pose size mismatch: solved=%d, reference=%d, mesh=%d"),
            Pose.Num(), Ref.GetNum(), BoneSpaceTransforms.Num());
        return false;
    }
    // Convert using the IMPORTED hierarchy (virtual re-parenting is solver-only).
    for (int32 I = 0; I < Pose.Num(); ++I)
    {
        const int32 P = Ref.GetParentIndex(I);
        BoneSpaceTransforms[I] = P == INDEX_NONE
            ? Pose[I] : Pose[I].GetRelativeTransform(Pose[P]);
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

    // Legs and tail are siblings of "Hip" in the imported skeleton; make them
    // follow the pelvis so AnimateHips moves the whole lower body.
    const TCHAR* Defaults[] = { TEXT("Hip_L"), TEXT("Hip_R"), TEXT("Tail") };
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
    // Super invokes Blueprint BeginPlay: initialize BEFORE its animation commands.
    Super::BeginPlay();
}

void ARatRigActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bRigReady) EvaluatePose(DeltaSeconds);
}

TArray<FName> ARatRigActor::GetChainBones(ERatChain Chain)
{
    TArray<FName> Out;
    auto Add = [&Out](const TCHAR* N) { Out.Add(FName(N)); };
    switch (Chain)
    {
    case ERatChain::Spine:
        Add(TEXT("Hip")); Add(TEXT("Torso")); Add(TEXT("Chest"));
        Add(TEXT("Neck")); Add(TEXT("Head"));
        break;
    case ERatChain::Tail:
        Add(TEXT("Tail"));
        for (int32 N = 1; N <= 6; ++N)
            Out.Add(FName(*FString::Printf(TEXT("Tail%d"), N)));
        break;
    case ERatChain::LegL:
        Add(TEXT("Hip_L")); Add(TEXT("Thigh_L")); Add(TEXT("Shin_L")); Add(TEXT("Foot_L"));
        break;
    case ERatChain::LegR:
        Add(TEXT("Hip_R")); Add(TEXT("Thigh_R")); Add(TEXT("Shin_R")); Add(TEXT("Foot_R"));
        break;
    case ERatChain::ArmL:
        Add(TEXT("Clav_L")); Add(TEXT("Shoulder_L")); Add(TEXT("Forearm_L")); Add(TEXT("Hand_L"));
        break;
    case ERatChain::ArmR:
        Add(TEXT("Clav_R")); Add(TEXT("Shoulder_R")); Add(TEXT("Forearm_R")); Add(TEXT("Hand_R"));
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
    Names.Empty(); Parents.Empty(); RestLocal.Empty(); RestComponent.Empty();
    USkeletalMesh* Asset = RatAsset;
    if (!Asset) Asset = Cast<USkeletalMesh>(RatMesh->GetSkinnedAsset());
    if (!Asset)
    {
        UE_LOG(LogTemp, Warning, TEXT("Rat: assign Rat Asset or Rat Mesh's Skeletal Mesh."));
        return false;
    }
    RatAsset = Asset;
    if (RatMesh->GetSkinnedAsset() != Asset)
        RatMesh->SetSkinnedAssetAndUpdate(Asset, true);
    const FReferenceSkeleton& Ref = RatAsset->GetRefSkeleton();
    const int32 Count = Ref.GetNum();
    if (Count == 0) return false;
    RestLocal = Ref.GetRefBonePose();
    Parents.SetNum(Count); Names.SetNum(Count); RestComponent.SetNum(Count);
    WorkingComponent.SetNum(Count); VisitState.SetNumZeroed(Count);
    for (int32 I = 0; I < Count; ++I)
    {
        Names[I] = Ref.GetBoneName(I);
        Parents[I] = Ref.GetParentIndex(I);
        RestComponent[I] = Parents[I] == INDEX_NONE
            ? RestLocal[I] : RestLocal[I] * RestComponent[Parents[I]];
    }

    // Virtual reparenting changes the evaluated pose, not the imported skeleton.
    // Preserve each bone's reference component transform to prevent snapping.
    for (const FRatAttachment& A : Attachments)
    {
        const int32 Bone = Names.IndexOfByKey(A.Bone);
        const int32 NewParent = Names.IndexOfByKey(A.NewParent);
        if (Bone == INDEX_NONE || NewParent == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("Rat: attachment skipped (%s -> %s): bone not found"),
                *A.Bone.ToString(), *A.NewParent.ToString());
            continue;
        }
        // Reject attachments that would make the bone its own ancestor.
        bool bCycle = false;
        for (int32 P = NewParent; P != INDEX_NONE; P = Parents[P])
        {
            if (P == Bone) { bCycle = true; break; }
        }
        if (bCycle)
        {
            UE_LOG(LogTemp, Warning, TEXT("Rat: rejected cyclic attachment %s -> %s"),
                *A.Bone.ToString(), *A.NewParent.ToString());
            continue;
        }
        Parents[Bone] = NewParent;
        RestLocal[Bone] = RestComponent[Bone].GetRelativeTransform(RestComponent[NewParent]);
        UE_LOG(LogTemp, Log, TEXT("Rat: %s follows %s"),
            *Names[Bone].ToString(), *Names[NewParent].ToString());
    }
    bRigReady = true;
    EvaluatePose(0.f);
    return true;
}

FQuat ARatRigActor::MakeOffset(float Horizontal, float Vertical, float Roll) const
{
    FVector H = HorizontalAxis.GetSafeNormal();
    FVector V = VerticalAxis.GetSafeNormal();
    FVector R = RollAxis.GetSafeNormal();
    if (H.IsNearlyZero()) H = FVector::UpVector;
    if (V.IsNearlyZero()) V = FVector::RightVector;
    if (R.IsNearlyZero()) R = FVector::ForwardVector;
    return (FQuat(V, FMath::DegreesToRadians(static_cast<double>(Vertical))) *
        FQuat(H, FMath::DegreesToRadians(static_cast<double>(Horizontal))) *
        FQuat(R, FMath::DegreesToRadians(static_cast<double>(Roll)))).GetNormalized();
}

// Map a rotation expressed in reference mesh axes into this joint's local reference axes.
FQuat ARatRigActor::ToJointLocal(int32 Index, const FQuat& ComponentOffset) const
{
    const FQuat R = RestComponent[Index].GetRotation();
    return (R.Inverse() * ComponentOffset * R).GetNormalized();
}

void ARatRigActor::SetMotion(int32 Index, FQuat Target, float Duration, bool Independent)
{
    ReleaseAfterReset.Remove(Index);
    // Convert the visible pose when switching spaces to avoid an initial jump.
    const FQuat ComponentQ = WorkingComponent[Index].GetRotation();
    const int32 Parent = Parents[Index];
    const FQuat ParentQ = Parent == INDEX_NONE ? FQuat::Identity
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

bool ARatRigActor::AnimateBone(FName BoneName, float HorizontalDegrees,
    float VerticalDegrees, float RollDegrees, float Duration, bool Independent)
{
    if (!bRigReady && !RebuildRig()) return false;
    const int32 I = Names.IndexOfByKey(BoneName);
    if (I == INDEX_NONE)
    {
        UE_LOG(LogTemp, Warning, TEXT("Rat: bone %s not found"), *BoneName.ToString());
        return false;
    }
    FQuat Target = MakeOffset(HorizontalDegrees, VerticalDegrees, RollDegrees);
    if (!Independent) Target = ToJointLocal(I, Target);
    DirectBoneEvents.Add(I);
    SetMotion(I, Target, Duration, Independent);
    EvaluatePose(0.f);
    return true;
}

bool ARatRigActor::AnimateChain(ERatChain Chain, FName StartBone,
    float HorizontalDegrees, float VerticalDegrees, float HorizontalStepDegrees,
    float VerticalStepDegrees, float Duration)
{
    if (!bRigReady && !RebuildRig()) return false;
    const TArray<FName> Bones = GetChainBones(Chain);
    int32 StartIdx = 0;
    if (!StartBone.IsNone())
    {
        StartIdx = Bones.IndexOfByKey(StartBone);
        if (StartIdx == INDEX_NONE)
        {
            UE_LOG(LogTemp, Warning, TEXT("Rat: %s is not part of the selected chain"), *StartBone.ToString());
            return false;
        }
    }
    // Validate the entire range before changing any event.
    for (int32 K = StartIdx; K < Bones.Num(); ++K)
    {
        if (!Names.Contains(Bones[K]))
        {
            UE_LOG(LogTemp, Warning, TEXT("Rat: missing chain bone %s"), *Bones[K].ToString());
            return false;
        }
    }
    for (int32 K = StartIdx; K < Bones.Num(); ++K)
    {
        const int32 I = Names.IndexOfByKey(Bones[K]);
        // A direct event on a bone has priority over this broad range event.
        if (DirectBoneEvents.Contains(I)) continue;
        const float Step = static_cast<float>(K - StartIdx);
        const FQuat Offset = MakeOffset(HorizontalDegrees + Step * HorizontalStepDegrees,
            VerticalDegrees + Step * VerticalStepDegrees, 0.f);
        SetMotion(I, ToJointLocal(I, Offset), Duration, false);
    }
    EvaluatePose(0.f);
    return true;
}

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

bool ARatRigActor::AnimateLegJoint(bool bLeft, ERatLegJoint Joint, float H, float V,
    float Roll, float Duration)
{
    const TCHAR* Side = bLeft ? TEXT("L") : TEXT("R");
    const TCHAR* Base = TEXT("Hip");
    switch (Joint)
    {
    case ERatLegJoint::Hip:   Base = TEXT("Hip");   break;
    case ERatLegJoint::Thigh: Base = TEXT("Thigh"); break;
    case ERatLegJoint::Shin:  Base = TEXT("Shin");  break;
    case ERatLegJoint::Foot:  Base = TEXT("Foot");  break;
    }
    return AnimateBone(FName(*FString::Printf(TEXT("%s_%s"), Base, Side)), H, V, Roll, Duration, false);
}

bool ARatRigActor::AnimateLeg(bool bLeft, float H, float V, float HStep, float VStep, float Duration)
{
    return AnimateChain(bLeft ? ERatChain::LegL : ERatChain::LegR,
        bLeft ? FName(TEXT("Thigh_L")) : FName(TEXT("Thigh_R")),
        H, V, HStep, VStep, Duration);
}

bool ARatRigActor::AnimateTail(float H, float V, float HStep, float VStep,
    float Duration, FName StartBone)
{
    return AnimateChain(ERatChain::Tail, StartBone, H, V, HStep, VStep, Duration);
}

bool ARatRigActor::ResetBone(FName BoneName, float Duration)
{
    if (!bRigReady) return false;
    const int32 I = Names.IndexOfByKey(BoneName);
    if (I == INDEX_NONE) return false;
    // Transition in local space so the released bone follows its parent smoothly.
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

void ARatRigActor::SolveBone(int32 Index)
{
    if (VisitState[Index] == 2) return;
    check(VisitState[Index] != 1);
    VisitState[Index] = 1;
    const int32 Parent = Parents[Index];
    if (Parent != INDEX_NONE) SolveBone(Parent);

    FTransform Local = RestLocal[Index];
    const FRatBoneMotion* M = Motions.Find(Index);
    if (M && !M->bIndependent)
        Local.SetRotation((Local.GetRotation() * M->Current).GetNormalized());

    WorkingComponent[Index] = Parent == INDEX_NONE
        ? Local : Local * WorkingComponent[Parent];

    if (M && M->bIndependent)
        WorkingComponent[Index].SetRotation((M->Current *
            RestComponent[Index].GetRotation()).GetNormalized());
    VisitState[Index] = 2;
}

void ARatRigActor::EvaluatePose(float DeltaSeconds)
{
    for (auto& Pair : Motions)
    {
        FRatBoneMotion& M = Pair.Value;
        M.Elapsed += FMath::Max(0.f, DeltaSeconds);
        const float T = M.Duration <= KINDA_SMALL_NUMBER ? 1.f
            : FMath::Clamp(M.Elapsed / M.Duration, 0.f, 1.f);
        const float Smooth = T * T * (3.f - 2.f * T);
        M.Current = FQuat::Slerp(M.Start, M.Target, Smooth).GetNormalized();
    }
    for (int32 I = 0; I < VisitState.Num(); ++I) VisitState[I] = 0;
    for (int32 I = 0; I < Names.Num(); ++I) SolveBone(I);
    URatPoseableMeshComponent* PoseMesh = Cast<URatPoseableMeshComponent>(RatMesh);
    if (PoseMesh) PoseMesh->PublishComponentPose(WorkingComponent);
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
