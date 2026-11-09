#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "RatMovementManager.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class RODEOS_API URatMovementManager : public UActorComponent
{
    GENERATED_BODY()

public:
    URatMovementManager();

    // The name of the bone you want to target (editable in Blueprint/Details)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rat Control")
    FName BoneName = FName("Head");

    // Blueprint callable function to move the specified bone (default parameter removed for UHT safety)
    UFUNCTION(BlueprintCallable, Category = "Rat Control")
    void MoveBoneTo(FRotator TargetRotation, TEnumAsByte<EBoneSpaces::Type> Space);

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY()
    UPoseableMeshComponent* CachedPoseableMesh;
};