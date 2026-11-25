#include "RatLandmarkTracker.h"

#include "RatRigActor.h"

#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "HttpModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ARatLandmarkTracker::ARatLandmarkTracker()
{
    PrimaryActorTick.bCanEverTick = false;

    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeAsset(
        TEXT("/Engine/BasicShapes/Cone.Cone")
    );

    if (ConeAsset.Succeeded())
    {
        MarkerMesh = ConeAsset.Object;
    }

    AddDefaultLandmarks();
}

void ARatLandmarkTracker::BeginPlay()
{
    Super::BeginPlay();

    RebuildMarkers();

    if (bEnableAgentPolling && GetWorld())
    {
        GetWorld()->GetTimerManager().SetTimer(
            PollTimerHandle,
            this,
            &ARatLandmarkTracker::PollCommandServer,
            PollIntervalSeconds,
            true,
            0.1f
        );
    }
}

void ARatLandmarkTracker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(PollTimerHandle);
    }

    ClearMarkers();

    Super::EndPlay(EndPlayReason);
}

void ARatLandmarkTracker::AddDefaultLandmarks()
{
    Landmarks.Empty();

    auto AddLandmark = [this](
        FName Name,
        FName Bone,
        FLinearColor Color = FLinearColor::Red)
        {
            FRatLandmark Landmark;

            Landmark.Name = Name;
            Landmark.Bone = Bone;
            Landmark.LocalOffset = FVector::ZeroVector;
            Landmark.LocalRotation = FRotator::ZeroRotator;
            Landmark.ConeScale = FVector(0.15f);
            Landmark.Color = Color;
            Landmark.DisplayOffset = FVector::ZeroVector;
            Landmark.DisplayRotation = FRotator::ZeroRotator;
            Landmark.DisplayScale = FVector::OneVector;

            Landmarks.Add(Landmark);
        };

    // Back
    AddLandmark(TEXT("Back1_Center"), TEXT("Chest"));
    AddLandmark(TEXT("Back1_Right"), TEXT("Chest"));
    AddLandmark(TEXT("Back2"), TEXT("Chest"));
    AddLandmark(TEXT("Back3"), TEXT("Chest"));
    AddLandmark(TEXT("Back4"), TEXT("Chest"));

    AddLandmark(TEXT("Back5"), TEXT("Torso"));
    AddLandmark(TEXT("Back6"), TEXT("Torso"));
    AddLandmark(TEXT("Back7"), TEXT("Torso"));
    AddLandmark(TEXT("Back8"), TEXT("Torso"));

    AddLandmark(TEXT("Back9"), TEXT("Hip"));
    AddLandmark(TEXT("Back10"), TEXT("Hip"));

    // Head
    AddLandmark(TEXT("TopOfHead"), TEXT("Head"));
    AddLandmark(TEXT("LeftEar"), TEXT("Ear_L"));
    AddLandmark(TEXT("RightEye"), TEXT("Head"));
    AddLandmark(TEXT("Nose"), TEXT("Nose"));
    AddLandmark(TEXT("RightEar"), TEXT("Ear_R"));

    // Upper body
    AddLandmark(TEXT("RightShoulder"), TEXT("Shoulder_R"));
    AddLandmark(TEXT("LeftPalm"), TEXT("Hand_L"));
    AddLandmark(TEXT("RightPalm"), TEXT("Hand_R"));
    AddLandmark(TEXT("RightWrist"), TEXT("Forearm_R"));
    AddLandmark(TEXT("RightArmpit"), TEXT("Shoulder_R"));
    AddLandmark(TEXT("RightVentrum"), TEXT("Torso"));

    // Legs
    AddLandmark(TEXT("RightLegFront"), TEXT("Thigh_R"));
    AddLandmark(TEXT("RightAnkle"), TEXT("Foot_R"));
    AddLandmark(TEXT("RightLegBehind"), TEXT("Hip_R"));

    // Tail base only
    AddLandmark(TEXT("TailBase"), TEXT("Hip"));
}

void ARatLandmarkTracker::ClearMarkers()
{
    for (UStaticMeshComponent* Marker : Markers)
    {
        if (IsValid(Marker))
        {
            Marker->DestroyComponent();
        }
    }

    Markers.Empty();
    MarkerByName.Empty();
}

void ARatLandmarkTracker::ApplyMarkerTransform(
    const FRatLandmark& Landmark,
    UStaticMeshComponent* MarkerComp) const
{
    if (!IsValid(MarkerComp))
    {
        return;
    }

    const FVector FinalLocation = Landmark.LocalOffset + Landmark.DisplayOffset;
    const FRotator FinalRotation = Landmark.LocalRotation + Landmark.DisplayRotation;
    const FVector FinalScale = FVector(
        Landmark.ConeScale.X * Landmark.DisplayScale.X,
        Landmark.ConeScale.Y * Landmark.DisplayScale.Y,
        Landmark.ConeScale.Z * Landmark.DisplayScale.Z
    );

    MarkerComp->SetRelativeLocation(FinalLocation);
    MarkerComp->SetRelativeRotation(FinalRotation);
    MarkerComp->SetRelativeScale3D(FinalScale);
}

void ARatLandmarkTracker::RebuildMarkers()
{
    ClearMarkers();

    RatMesh = nullptr;

    if (!IsValid(RatActor))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("RAT TRACKER: RatActor is not assigned or invalid.")
        );

        return;
    }

    // Obtain the actual poseable mesh from your rat rig actor.
    RatMesh = RatActor->RatMesh;

    if (!IsValid(RatMesh))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("RAT TRACKER: ARatRigActor has no valid RatMesh component.")
        );

        return;
    }

    if (!RatMesh->GetSkinnedAsset())
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("RAT TRACKER: RatMesh has no skeletal mesh asset.")
        );

        return;
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("RAT TRACKER: Found poseable mesh: %s"),
        *RatMesh->GetName()
    );

    CreateMarkers();
}

void ARatLandmarkTracker::CreateMarkers()
{
    if (!IsValid(RatMesh) || !IsValid(MarkerMesh))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("RAT TRACKER: RatMesh or MarkerMesh is invalid.")
        );

        return;
    }

    int32 CreatedCount = 0;

    for (const FRatLandmark& Landmark : Landmarks)
    {
        const int32 BoneIndex = RatMesh->GetBoneIndex(Landmark.Bone);

        if (BoneIndex == INDEX_NONE)
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("RAT TRACKER: Bone '%s' not found for landmark '%s'."),
                *Landmark.Bone.ToString(),
                *Landmark.Name.ToString()
            );

            continue;
        }

        UStaticMeshComponent* Marker =
            NewObject<UStaticMeshComponent>(this);

        if (!IsValid(Marker))
        {
            continue;
        }

        Marker->SetStaticMesh(MarkerMesh);

        Marker->SetMobility(EComponentMobility::Movable);

        Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);

        Marker->SetGenerateOverlapEvents(false);

        Marker->SetVisibility(true, true);

        Marker->SetHiddenInGame(false);

        // Attach directly to the poseable mesh bone.
        Marker->SetupAttachment(RatMesh, Landmark.Bone);

        ApplyMarkerTransform(Landmark, Marker);

        AddInstanceComponent(Marker);

        Marker->RegisterComponent();

        if (IsValid(MarkerMaterial))
        {
            UMaterialInstanceDynamic* DynamicMaterial =
                UMaterialInstanceDynamic::Create(
                    MarkerMaterial,
                    Marker
                );

            if (IsValid(DynamicMaterial))
            {
                DynamicMaterial->SetVectorParameterValue(
                    ColorParameter,
                    Landmark.Color
                );

                Marker->SetMaterial(0, DynamicMaterial);
            }
        }

        Markers.Add(Marker);
        MarkerByName.Add(Landmark.Name, Marker);

        ++CreatedCount;

        UE_LOG(
            LogTemp,
            Display,
            TEXT("RAT TRACKER: Created '%s' on bone '%s'."),
            *Landmark.Name.ToString(),
            *Landmark.Bone.ToString()
        );
    }

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("RAT TRACKER: Successfully created %d of %d markers."),
        CreatedCount,
        Landmarks.Num()
    );
}

bool ARatLandmarkTracker::SetMarkerDisplayTransform(
    FName LandmarkName,
    FVector Offset,
    FRotator RotationOffset,
    FVector ScaleMultiplier)
{
    TObjectPtr<UStaticMeshComponent>* Found = MarkerByName.Find(LandmarkName);
    if (!Found || !IsValid(*Found))
    {
        return false;
    }

    const int32 Index = Landmarks.IndexOfByPredicate(
        [&](const FRatLandmark& Entry)
        {
            return Entry.Name == LandmarkName;
        });

    if (Index == INDEX_NONE)
    {
        return false;
    }

    FRatLandmark& Landmark = Landmarks[Index];
    Landmark.DisplayOffset = Offset;
    Landmark.DisplayRotation = RotationOffset;
    Landmark.DisplayScale = ScaleMultiplier;

    ApplyMarkerTransform(Landmark, Found->Get());
    return true;
}

bool ARatLandmarkTracker::SetMarkerDisplayOffset(FName LandmarkName, FVector Offset)
{
    const int32 Index = Landmarks.IndexOfByPredicate(
        [&](const FRatLandmark& Entry)
        {
            return Entry.Name == LandmarkName;
        });

    if (Index == INDEX_NONE)
    {
        return false;
    }

    const FRatLandmark& Landmark = Landmarks[Index];
    return SetMarkerDisplayTransform(
        LandmarkName,
        Offset,
        Landmark.DisplayRotation,
        Landmark.DisplayScale
    );
}

void ARatLandmarkTracker::ResetMarkerDisplayTransforms()
{
    for (FRatLandmark& Landmark : Landmarks)
    {
        SetMarkerDisplayTransform(
            Landmark.Name,
            FVector::ZeroVector,
            FRotator::ZeroRotator,
            FVector::OneVector
        );
    }
}

bool ARatLandmarkTracker::GetMarkerWorldLocation(FName LandmarkName, FVector& WorldLocation) const
{
    const TObjectPtr<UStaticMeshComponent>* Found = MarkerByName.Find(LandmarkName);
    if (!Found || !IsValid(*Found))
    {
        return false;
    }

    WorldLocation = (*Found)->GetComponentLocation();
    return true;
}

TArray<FName> ARatLandmarkTracker::GetAllLandmarkNames() const
{
    TArray<FName> Names;
    Names.Reserve(Landmarks.Num());
    for (const FRatLandmark& Landmark : Landmarks)
    {
        Names.Add(Landmark.Name);
    }
    return Names;
}

void ARatLandmarkTracker::PollCommandServer()
{
    if (bRequestInFlight || CommandServerBaseUrl.IsEmpty())
    {
        return;
    }

    FString Url = CommandServerBaseUrl;
    while (Url.EndsWith(TEXT("/")))
    {
        Url.RemoveFromEnd(TEXT("/"));
    }
    Url += TEXT("/poll");

    TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest =
        FHttpModule::Get().CreateRequest();
    HttpRequest->SetURL(Url);
    HttpRequest->SetVerb(TEXT("GET"));
    HttpRequest->SetTimeout(1.5f);
    HttpRequest->OnProcessRequestComplete().BindUObject(
        this,
        &ARatLandmarkTracker::OnPollResponseReceived
    );

    bRequestInFlight = true;
    if (!HttpRequest->ProcessRequest())
    {
        bRequestInFlight = false;
    }
}

void ARatLandmarkTracker::OnPollResponseReceived(
    FHttpRequestPtr Request,
    FHttpResponsePtr Response,
    bool bConnectedSuccessfully)
{
    bRequestInFlight = false;

    if (!bConnectedSuccessfully || !Response.IsValid())
    {
        return;
    }

    if (Response->GetResponseCode() < 200 || Response->GetResponseCode() >= 300)
    {
        return;
    }

    const FString Content = Response->GetContentAsString();
    if (Content.IsEmpty())
    {
        return;
    }

    TSharedPtr<FJsonObject> RootObj;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Content);
    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
    {
        return;
    }

    bool bResetAll = false;
    if (RootObj->TryGetBoolField(TEXT("reset"), bResetAll) && bResetAll)
    {
        ResetMarkerDisplayTransforms();
    }

    const TArray<TSharedPtr<FJsonValue>>* CommandsArray = nullptr;
    if (RootObj->TryGetArrayField(TEXT("commands"), CommandsArray) && CommandsArray)
    {
        for (const TSharedPtr<FJsonValue>& Val : *CommandsArray)
        {
            if (Val.IsValid() && Val->Type == EJson::Object)
            {
                ApplyCommandJsonObject(Val->AsObject());
            }
        }
    }
}

void ARatLandmarkTracker::ApplyCommandJsonObject(const TSharedPtr<FJsonObject>& CmdObj)
{
    if (!CmdObj.IsValid())
    {
        return;
    }

    FString LandmarkStr;
    if (!CmdObj->TryGetStringField(TEXT("landmark"), LandmarkStr) || LandmarkStr.IsEmpty())
    {
        return;
    }

    const FName LandmarkName(*LandmarkStr);
    const int32 Index = Landmarks.IndexOfByPredicate(
        [&](const FRatLandmark& Entry)
        {
            return Entry.Name == LandmarkName;
        });

    if (Index == INDEX_NONE)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("RAT TRACKER: Agent command referenced unknown landmark '%s'."),
            *LandmarkStr
        );
        return;
    }

    const FRatLandmark& Current = Landmarks[Index];
    FVector NewOffset = Current.DisplayOffset;
    FRotator NewRotation = Current.DisplayRotation;
    FVector NewScale = Current.DisplayScale;

    auto ReadVec3 = [&](const TCHAR* FieldName, FVector& OutVec)
    {
        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        if (CmdObj->TryGetArrayField(FieldName, Arr) && Arr && Arr->Num() == 3)
        {
            double X = 0.0, Y = 0.0, Z = 0.0;
            if ((*Arr)[0]->TryGetNumber(X) && (*Arr)[1]->TryGetNumber(Y) && (*Arr)[2]->TryGetNumber(Z))
            {
                OutVec = FVector(
                    static_cast<float>(X),
                    static_cast<float>(Y),
                    static_cast<float>(Z)
                );
            }
        }
    };

    // Supports "offset_cm" (preferred) and "offset" as aliases.
    ReadVec3(TEXT("offset_cm"), NewOffset);
    ReadVec3(TEXT("offset"), NewOffset);

    // Supports "rotation_deg" as [pitch, yaw, roll].
    const TArray<TSharedPtr<FJsonValue>>* RotArr = nullptr;
    if (CmdObj->TryGetArrayField(TEXT("rotation_deg"), RotArr) && RotArr && RotArr->Num() == 3)
    {
        double Pitch = 0.0, Yaw = 0.0, Roll = 0.0;
        if ((*RotArr)[0]->TryGetNumber(Pitch) && (*RotArr)[1]->TryGetNumber(Yaw) && (*RotArr)[2]->TryGetNumber(Roll))
        {
            NewRotation = FRotator(
                static_cast<float>(Pitch),
                static_cast<float>(Yaw),
                static_cast<float>(Roll)
            );
        }
    }

    // Supports "scale_multiplier" and "scale".
    ReadVec3(TEXT("scale_multiplier"), NewScale);
    ReadVec3(TEXT("scale"), NewScale);

    SetMarkerDisplayTransform(LandmarkName, NewOffset, NewRotation, NewScale);
}