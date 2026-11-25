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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rat Control")
    FName BoneName = FName("Head");

    UFUNCTION(BlueprintCallable, Category = "Rat Control")
    void MoveBoneTo(FRotator TargetRotation, TEnumAsByte<EBoneSpaces::Type> Space);

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY()
    UPoseableMeshComponent* CachedPoseableMesh;
};