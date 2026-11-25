
#include "RatPoseClientComponent.h"

URatPoseClientComponent::URatPoseClientComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void URatPoseClientComponent::BeginPlay()
{
    Super::BeginPlay();
    ConnectToRig();
}

bool URatPoseClientComponent::ConnectToRig()
{
    TargetRig = Cast<ARatRigActor>(GetOwner());

    bConnected = IsValid(TargetRig);

    if (!bConnected)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("RatPoseClient: Owner is not an ARatRigActor."));
        return false;
    }

    UE_LOG(LogTemp, Log,
        TEXT("RatPoseClient: Connected to %s"),
        *TargetRig->GetName());

    return true;
}

bool URatPoseClientComponent::MoveBone(
    FName BoneName,
    float Horizontal,
    float Vertical,
    float Roll,
    float Duration)
{
    if (!IsValid(TargetRig) && !ConnectToRig())
        return false;

    return TargetRig->AnimateBone(
        BoneName,
        Horizontal,
        Vertical,
        Roll,
        Duration,
        false
    );
}

bool URatPoseClientComponent::MoveHead(
    float H,
    float V,
    float Roll,
    float Duration)
{
    if (!IsValid(TargetRig) && !ConnectToRig())
        return false;

    return TargetRig->AnimateHead(H, V, Roll, Duration);
}

bool URatPoseClientComponent::MoveNeck(
    float H,
    float V,
    float Roll,
    float Duration)
{
    if (!IsValid(TargetRig) && !ConnectToRig())
        return false;

    return TargetRig->AnimateNeck(H, V, Roll, Duration);
}

bool URatPoseClientComponent::MoveLeg(
    bool bLeft,
    float H,
    float V,
    float HStep,
    float VStep,
    float Duration)
{
    if (!IsValid(TargetRig) && !ConnectToRig())
        return false;

    return TargetRig->AnimateLeg(
        bLeft,
        H,
        V,
        HStep,
        VStep,
        Duration
    );
}

bool URatPoseClientComponent::MoveTail(
    float H,
    float V,
    float HStep,
    float VStep,
    float Duration)
{
    if (!IsValid(TargetRig) && !ConnectToRig())
        return false;

    return TargetRig->AnimateTail(
        H,
        V,
        HStep,
        VStep,
        Duration
    );
}

bool URatPoseClientComponent::ApplyPoseFrame(
    const TArray<FRatBoneTarget>& Targets)
{
    if (!IsValid(TargetRig) && !ConnectToRig())
        return false;

    // Apply the complete incoming pose in one batch.
    // RatRigActor evaluates and publishes the pose only once.
    return TargetRig->ApplyBoneTargets(Targets);
}

void URatPoseClientComponent::ResetRat(float Duration)
{
    if (!IsValid(TargetRig) && !ConnectToRig())
        return;

    TargetRig->ResetAll(Duration);
}
