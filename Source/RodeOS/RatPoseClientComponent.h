
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RatRigActor.h"
#include "RatPoseClientComponent.generated.h"

// Client component that forwards pose commands to ARatRigActor.
UCLASS(ClassGroup = (SWARMOSCOPE), meta = (BlueprintSpawnableComponent))
class URatPoseClientComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    URatPoseClientComponent();

    virtual void BeginPlay() override;

    // Resolve the RatRigActor that owns this component.
    UFUNCTION(BlueprintCallable, Category = "Rat Client")
    bool ConnectToRig();

    // Individual bone control.
    UFUNCTION(BlueprintCallable, Category = "Rat Client")
    bool MoveBone(
        FName BoneName,
        float Horizontal,
        float Vertical,
        float Roll = 0.f,
        float Duration = 0.5f
    );

    // Convenience controls.
    UFUNCTION(BlueprintCallable, Category = "Rat Client")
    bool MoveHead(
        float H,
        float V,
        float Roll = 0.f,
        float Duration = 0.5f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat Client")
    bool MoveNeck(
        float H,
        float V,
        float Roll = 0.f,
        float Duration = 0.5f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat Client")
    bool MoveLeg(
        bool bLeft,
        float H,
        float V,
        float HStep = 0.f,
        float VStep = 0.f,
        float Duration = 0.5f
    );

    UFUNCTION(BlueprintCallable, Category = "Rat Client")
    bool MoveTail(
        float H,
        float V,
        float HStep = 0.f,
        float VStep = 0.f,
        float Duration = 0.5f
    );

    // Apply multiple bone targets as one incoming pose frame.
    // FRatBoneTarget is declared in RatRigActor.h.
    UFUNCTION(BlueprintCallable, Category = "Rat Client")
    bool ApplyPoseFrame(const TArray<FRatBoneTarget>& Targets);

    UFUNCTION(BlueprintCallable, Category = "Rat Client")
    void ResetRat(float Duration = 0.5f);

    UPROPERTY(BlueprintReadOnly, Category = "Rat Client")
    bool bConnected = false;

private:
    UPROPERTY()
    TObjectPtr<ARatRigActor> TargetRig = nullptr;
};
