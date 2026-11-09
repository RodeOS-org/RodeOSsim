#include "RatMovementManager.h"
#include "Engine/Engine.h"

URatMovementManager::URatMovementManager()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void URatMovementManager::BeginPlay()
{
    Super::BeginPlay();

    if (AActor* Owner = GetOwner())
    {
        CachedPoseableMesh = Owner->FindComponentByClass<UPoseableMeshComponent>();
    }
}

void URatMovementManager::MoveBoneTo(FRotator TargetRotation, TEnumAsByte<EBoneSpaces::Type> Space)
{
    if (!CachedPoseableMesh && GetOwner())
    {
        CachedPoseableMesh = GetOwner()->FindComponentByClass<UPoseableMeshComponent>();
    }

    if (!CachedPoseableMesh)
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("MoveBoneTo Failed: No PoseableMeshComponent found!"));
        }
        return;
    }

    if (BoneName.IsNone())
    {
        return;
    }

    // Apply rotation directly to the chosen bone
    CachedPoseableMesh->SetBoneRotationByName(BoneName, TargetRotation, Space.GetValue());
}