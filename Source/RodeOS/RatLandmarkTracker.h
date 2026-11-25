#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "RatLandmarkTracker.generated.h"

class ARatRigActor;
class UPoseableMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FRatLandmark
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
    FName Name = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
    FName Bone = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
    FVector LocalOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
    FRotator LocalRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
    FVector ConeScale = FVector(0.02f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark")
    FLinearColor Color = FLinearColor::Red;

    // Runtime display adjustments applied on top of base placement (never alters bones).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark|Display")
    FVector DisplayOffset = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark|Display")
    FRotator DisplayRotation = FRotator::ZeroRotator;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmark|Display")
    FVector DisplayScale = FVector::OneVector;
};

UCLASS(Blueprintable)
class RODEOS_API ARatLandmarkTracker : public AActor
{
    GENERATED_BODY()

public:
    ARatLandmarkTracker();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
    // Assign the ARatRigActor Blueprint instance from the level (e.g. New_Posable_Rat).
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "Rat")
    TObjectPtr<ARatRigActor> RatActor = nullptr;

    // Automatically obtained from ARatRigActor::RatMesh.
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rat")
    TObjectPtr<UPoseableMeshComponent> RatMesh = nullptr;

    // Cone mesh used for markers.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers")
    TObjectPtr<UStaticMesh> MarkerMesh = nullptr;

    // Optional marker material.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers")
    TObjectPtr<UMaterialInterface> MarkerMaterial = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Markers")
    FName ColorParameter = TEXT("BaseColor");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Landmarks")
    TArray<FRatLandmark> Landmarks;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> Markers;

    UPROPERTY(Transient)
    TMap<FName, TObjectPtr<UStaticMeshComponent>> MarkerByName;

    // FastAPI agent command bridge settings (polled during Play).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RodeOS|AgentBridge")
    bool bEnableAgentPolling = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RodeOS|AgentBridge")
    FString CommandServerBaseUrl = TEXT("http://127.0.0.1:8001");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RodeOS|AgentBridge", meta = (ClampMin = "0.02", ClampMax = "5.0"))
    float PollIntervalSeconds = 0.25f;

    UFUNCTION(BlueprintCallable, Category = "Rat|Landmarks")
    void RebuildMarkers();

    UFUNCTION(BlueprintCallable, Category = "RodeOS|Landmarks")
    bool SetMarkerDisplayTransform(
        FName LandmarkName,
        FVector Offset,
        FRotator RotationOffset,
        FVector ScaleMultiplier);

    UFUNCTION(BlueprintCallable, Category = "RodeOS|Landmarks")
    bool SetMarkerDisplayOffset(FName LandmarkName, FVector Offset);

    UFUNCTION(BlueprintCallable, Category = "RodeOS|Landmarks")
    void ResetMarkerDisplayTransforms();

    UFUNCTION(BlueprintCallable, Category = "RodeOS|Landmarks")
    bool GetMarkerWorldLocation(FName LandmarkName, FVector& WorldLocation) const;

    UFUNCTION(BlueprintCallable, Category = "RodeOS|Landmarks")
    TArray<FName> GetAllLandmarkNames() const;

private:
    FTimerHandle PollTimerHandle;
    bool bRequestInFlight = false;

    void AddDefaultLandmarks();
    void ClearMarkers();
    void CreateMarkers();
    void ApplyMarkerTransform(const FRatLandmark& Landmark, UStaticMeshComponent* MarkerComp) const;

    void PollCommandServer();
    void OnPollResponseReceived(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bConnectedSuccessfully);
    void ApplyCommandJsonObject(const TSharedPtr<FJsonObject>& CmdObj);
};