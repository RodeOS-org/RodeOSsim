#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/PoseableMeshComponent.h"
#include "RatRigActor.generated.h"

class USkeletalMesh;

// Publishes a complete component-space pose in one batch, relative to the imported hierarchy.
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
    Spine   UMETA(DisplayName="Spine"),
    Tail    UMETA(DisplayName="Tail"),
    LegL    UMETA(DisplayName="Left Leg"),
    LegR    UMETA(DisplayName="Right Leg"),
    ArmL    UMETA(DisplayName="Left Arm"),
    ArmR    UMETA(DisplayName="Right Arm")
};

UENUM(BlueprintType)
enum class ERatLegJoint : uint8
{
    Hip,    // Hip_L / Hip_R
    Thigh,
    Shin,
    Foot
};

// Rigid virtual re-parenting: the bone keeps its reference component transform
// (no snapping) but follows NewParent in the evaluated pose.
// Needed because Hip_L, Hip_R and Tail are siblings of "Hip" under Hip-Controller,
// so rotating "Hip" would otherwise not move the legs or tail.
USTRUCT(BlueprintType)
struct FRatAttachment
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attachment")
    FName Bone = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attachment")
    FName NewParent = NAME_None;
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

// Compile these files inside the game module. No placeholder API macro is used.
UCLASS(Blueprintable)
class ARatRigActor : public AActor
{
    GENERATED_BODY()

public:
    ARatRigActor();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rat")
    UPoseableMeshComponent* RatMesh;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rat")
    USkeletalMesh* RatAsset = nullptr;

    // Axes are in mesh/component coordinates in the reference pose.
    // Horizontal = yaw (left/right swing), Vertical = pitch (up/down), Roll = twist.
    // If a motion goes the wrong way, flip/swap these three in the Details panel.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Axes")
    FVector HorizontalAxis = FVector::UpVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Axes")
    FVector VerticalAxis = FVector::RightVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Axes")
    FVector RollAxis = FVector::ForwardVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rig")
    TArray<FRatAttachment> Attachments;

    UFUNCTION(BlueprintCallable, Category="Rat|Setup")
    bool RebuildRig();

    // ---- Generic -----------------------------------------------------------
    // Offsets from the reference pose; Duration is transition time, not hold time.
    // Independent=true locks this bone's orientation to its own component-space
    // target. Its location still follows its parent to preserve the joint.
    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool AnimateBone(FName BoneName, float HorizontalDegrees, float VerticalDegrees,
        float RollDegrees = 0.f, float Duration = 0.5f, bool Independent = false);

    // Drives StartBone and every following bone of the chain to its end.
    // Per-bone step values accumulate along the chain (wave / curl effects).
    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool AnimateChain(ERatChain Chain, FName StartBone, float HorizontalDegrees,
        float VerticalDegrees, float HorizontalStepDegrees = 0.f,
        float VerticalStepDegrees = 0.f, float Duration = 0.5f);

    // ---- Body parts --------------------------------------------------------
    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool AnimateHead(float HorizontalDegrees, float VerticalDegrees,
        float RollDegrees = 0.f, float Duration = 0.5f);

    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool AnimateNeck(float HorizontalDegrees, float VerticalDegrees,
        float RollDegrees = 0.f, float Duration = 0.5f);

    // Rotates the "Hip" pelvis bone (legs and tail follow via Attachments).
    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool AnimateHips(float HorizontalDegrees, float VerticalDegrees,
        float RollDegrees = 0.f, float Duration = 0.5f);

    // Single leg joint, e.g. (Left, Thigh).
    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool AnimateLegJoint(bool bLeft, ERatLegJoint Joint, float HorizontalDegrees,
        float VerticalDegrees, float RollDegrees = 0.f, float Duration = 0.5f);

    // Whole leg from Thigh down, with per-joint steps (default start: Thigh).
    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool AnimateLeg(bool bLeft, float HorizontalDegrees, float VerticalDegrees,
        float HorizontalStepDegrees = 0.f, float VerticalStepDegrees = 0.f,
        float Duration = 0.5f);

    // Tail from StartBone (None = "Tail") to Tail6.
    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool AnimateTail(float HorizontalDegrees, float VerticalDegrees,
        float HorizontalStepDegrees = 0.f, float VerticalStepDegrees = 0.f,
        float Duration = 0.5f, FName StartBone = NAME_None);

    // ---- Reset -------------------------------------------------------------
    // Smoothly straighten this bone, then release its independent override.
    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool ResetBone(FName BoneName, float Duration = 0.5f);

    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
    bool ResetChain(ERatChain Chain, float Duration = 0.5f);

    UFUNCTION(BlueprintCallable, Category="Rat|Animation")
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
    FQuat MakeOffset(float Horizontal, float Vertical, float Roll) const;
    FQuat ToJointLocal(int32 Index, const FQuat& ComponentOffset) const;
    void SetMotion(int32 Index, FQuat Target, float Duration, bool Independent);
    void EvaluatePose(float DeltaSeconds);
    void SolveBone(int32 Index);
};
