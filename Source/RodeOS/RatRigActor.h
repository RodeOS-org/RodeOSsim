#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/PoseableMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "RatRigActor.generated.h"

// Publishes a complete component-space pose in one batch,
// relative to the imported hierarchy.
UCLASS()
class URatPoseableMeshComponent : public UPoseableMeshComponent
{
    GENERATED_BODY()

public:
    bool PublishComponentPose(const TArray<FTransform>& Pose);
};

// Bone chains that can be driven as a unit (with per-bone step offsets).
UENUM(BlueprintType)
enum class ERatChain : uint8
{
    Spine   UMETA(DisplayName = "Spine"),
    Tail    UMETA(DisplayName = "Tail"),
    LegL    UMETA(DisplayName = "Left Rear Leg"),
    LegR    UMETA(DisplayName = "Right Rear Leg"),
    ArmL    UMETA(DisplayName = "Left Front Leg"),
    ArmR    UMETA(DisplayName = "Right Front Leg")
};

UENUM(BlueprintType)
enum class ERatLegJoint : uint8
{
    Hip,    // Hip_L / Hip_R
    Thigh,
    Shin,
    Foot
};

UENUM(BlueprintType)
enum class ERatArmJoint : uint8
{
    Clavicle, // Clav_L / Clav_R
    Shoulder, // Shoulder_L / Shoulder_R
    Forearm,  // Forearm_L / Forearm_R
    Hand      // Hand_L / Hand_R
};

// Rigid virtual re-parenting.
USTRUCT(BlueprintType)
struct FRatAttachment
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment")
    FName Bone = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment")
    FName NewParent = NAME_None;
};

// Target rotation for one bone in a streamed pose frame.
USTRUCT(BlueprintType)
struct FRatBoneTarget
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rat Pose")
    FName BoneName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rat Pose")
    float Horizontal = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rat Pose")
    float Vertical = 0.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rat Pose")
    float Roll = 0.f;
};

struct FRatBoneMotion
{
    FQuat Start = FQuat::Identity;
    FQuat Target = FQuat::Identity;
    FQuat Current = FQuat::Identity;
    float Elapsed = 0.f;
    float Duration = 0.f;
    bool bIndependent = false;
};

UCLASS(Blueprintable)
class ARatRigActor : public AActor
{
    GENERATED_BODY()

public:
    ARatRigActor();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rat")
    UPoseableMeshComponent* RatMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Rat")
    USkeletalMesh* RatAsset = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Axes")
    FVector HorizontalAxis = FVector::UpVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Axes")
    FVector VerticalAxis = FVector::RightVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Axes")
    FVector RollAxis = FVector::ForwardVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rig")
    TArray<FRatAttachment> Attachments;

    UFUNCTION(BlueprintCallable, Category = "Rat|Setup")
    bool RebuildRig();

    // Apply multiple bone targets as one pose update.
    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool ApplyBoneTargets(const TArray<FRatBoneTarget>& Targets);

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateBone(
        FName BoneName,
        float HorizontalDegrees,
        float VerticalDegrees,
        float RollDegrees = 0.f,
        float Duration = 0.5f,
        bool Independent = false
    );

    // Direct Pitch / Yaw / Roll rotation in degrees (via FRotator).
    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateBoneRotator(
        FName BoneName,
        FRotator RotationDegrees,
        float Duration = 0.5f,
        bool Independent = false
    );

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateChain(
        ERatChain Chain,
        FName StartBone,
        float HorizontalDegrees,
        float VerticalDegrees,
        float HorizontalStepDegrees = 0.f,
        float VerticalStepDegrees = 0.f,
        float Duration = 0.5f,
        float RollDegrees = 0.f,
        float RollStepDegrees = 0.f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateHead(
        float HorizontalDegrees,
        float VerticalDegrees,
        float RollDegrees = 0.f,
        float Duration = 0.5f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateNeck(
        float HorizontalDegrees,
        float VerticalDegrees,
        float RollDegrees = 0.f,
        float Duration = 0.5f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateHips(
        float HorizontalDegrees,
        float VerticalDegrees,
        float RollDegrees = 0.f,
        float Duration = 0.5f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateLegJoint(
        bool bLeft,
        ERatLegJoint Joint,
        float HorizontalDegrees,
        float VerticalDegrees,
        float RollDegrees = 0.f,
        float Duration = 0.5f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateArmJoint(
        bool bLeft,
        ERatArmJoint Joint,
        float HorizontalDegrees,
        float VerticalDegrees,
        float RollDegrees = 0.f,
        float Duration = 0.5f
    );

    // Animates rear legs by default, or front legs when bFront is true.
    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateLeg(
        bool bLeft,
        float HorizontalDegrees,
        float VerticalDegrees,
        float HorizontalStepDegrees = 0.f,
        float VerticalStepDegrees = 0.f,
        float Duration = 0.5f,
        float RollDegrees = 0.f,
        float RollStepDegrees = 0.f,
        bool bFront = false
    );

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateArm(
        bool bLeft,
        float HorizontalDegrees,
        float VerticalDegrees,
        float HorizontalStepDegrees = 0.f,
        float VerticalStepDegrees = 0.f,
        float Duration = 0.5f,
        float RollDegrees = 0.f,
        float RollStepDegrees = 0.f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool AnimateTail(
        float HorizontalDegrees,
        float VerticalDegrees,
        float HorizontalStepDegrees = 0.f,
        float VerticalStepDegrees = 0.f,
        float Duration = 0.5f,
        FName StartBone = NAME_None,
        float RollDegrees = 0.f,
        float RollStepDegrees = 0.f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool ResetBone(FName BoneName, float Duration = 0.5f);

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    bool ResetChain(ERatChain Chain, float Duration = 0.5f);

    UFUNCTION(BlueprintCallable, Category = "Rat|Animation")
    void ResetAll(float Duration = 0.5f);

private:
    TArray<FName> Names;
    TArray<int32> Parents;
    TArray<FTransform> RestLocal;
    TArray<FTransform> RestComponent;
    TArray<FTransform> WorkingComponent;
    TArray<uint8> VisitState;

    TMap<int32, FRatBoneMotion> Motions;
    TSet<int32> ReleaseAfterReset;
    TSet<int32> DirectBoneEvents;

    bool bRigReady = false;

    static TArray<FName> GetChainBones(ERatChain Chain);

    FQuat MakeOffset(
        float Horizontal,
        float Vertical,
        float Roll
    ) const;

    FQuat ToJointLocal(
        int32 Index,
        const FQuat& ComponentOffset
    ) const;

    void SetMotion(
        int32 Index,
        FQuat Target,
        float Duration,
        bool Independent
    );

    void EvaluatePose(float DeltaSeconds);

    void SolveBone(int32 Index);
};