// Copyright (c) 2017 Computer Vision Center (CVC) at the Universitat Autonoma de Barcelona (UAB). This work is licensed under the terms of the MIT license. For a copy, see <https://opensource.org/licenses/MIT>.

#include "OpenDriveToMap.h"
#include "Components/Button.h"
#include "DesktopPlatform/Public/IDesktopPlatform.h"
#include "DesktopPlatform/Public/DesktopPlatformModule.h"
#include "Misc/FileHelper.h"

#include "Carla/Game/CarlaStatics.h"
#include "Traffic/TrafficLightManager.h"
#include "Util/ProceduralCustomMesh.h"

#include "OpenDrive/OpenDriveGenerator.h"

#include <compiler/disable-ue4-macros.h>
#include <carla/opendrive/OpenDriveParser.h>
#include <carla/road/Map.h>
#include <carla/rpc/String.h>
#include <compiler/enable-ue4-macros.h>

#include "Engine/Classes/Interfaces/Interface_CollisionDataProvider.h"
#include "PhysicsCore/Public/BodySetupEnums.h"
#include "RawMesh.h"
#include "AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "MeshDescription.h"
#include "ProceduralMeshConversion.h"

void UOpenDriveToMap::NativeConstruct()
{
  Super::NativeConstruct();
  if( IsValid(ChooseFileButon) ){
    ChooseFileButon->OnClicked.AddDynamic( this, &UOpenDriveToMap::CreateMap );
  }

}

void UOpenDriveToMap::NativeDestruct()
{
  Super::NativeDestruct();
  if( IsValid(ChooseFileButon) ){
    ChooseFileButon->OnClicked.RemoveDynamic( this, &UOpenDriveToMap::CreateMap );
  }

}

void UOpenDriveToMap::CreateMap()
{
  OpenFileDialog();
  LoadMap();
}

void UOpenDriveToMap::OpenFileDialog()
{
  TArray<FString> OutFileNames;
  void* ParentWindowPtr = FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle();
  IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
  if (DesktopPlatform)
  {
    DesktopPlatform->OpenFileDialog(ParentWindowPtr, "Select xodr file", FPaths::ProjectDir(), FString(""), ".xodr", 1, OutFileNames);
  }
  for(FString& CurrentString : OutFileNames)
  {
    FilePath = CurrentString;
    UE_LOG(LogCarlaToolsMapGenerator, Log, TEXT("FileObtained %s"), *CurrentString );
  }
}

void UOpenDriveToMap::LoadMap()
{
  FString FileContent;
  FFileHelper::LoadFileToString(FileContent, *FilePath);
  std::string opendrive_xml = carla::rpc::FromLongFString(FileContent);
  boost::optional<carla::road::Map> CarlaMap = carla::opendrive::OpenDriveParser::Load(opendrive_xml);

  if (!CarlaMap.has_value()) 
  {
    UE_LOG(LogCarlaToolsMapGenerator, Error, TEXT("Invalid Map"));
  }else
  {
    UE_LOG(LogCarlaToolsMapGenerator, Error, TEXT("Valid Map loaded"));
  }
  MapName = FPaths::GetCleanFilename(FilePath);
  int32 CharIndex = 0;
  MapName.FindChar('.', CharIndex);
  MapName = MapName.LeftChop(CharIndex - 1);
  GenerateAll(CarlaMap);
}

void UOpenDriveToMap::GenerateAll(const boost::optional<carla::road::Map>& CarlaMap )
{
  if (!CarlaMap.has_value()) 
  {
    UE_LOG(LogCarlaToolsMapGenerator, Error, TEXT("Invalid Map"));
  }else
  {
    GenerateRoadMesh(CarlaMap);
    GenerateSpawnPoints(CarlaMap);
  }
}


void UOpenDriveToMap::GenerateRoadMesh( const boost::optional<carla::road::Map>& CarlaMap )
{
  const auto Meshes = CarlaMap->GenerateChunkedMesh(opg_parameters);
  TArray<AActor*> ActorMeshList;
  TArray<UStaticMesh*> MeshesToSpawn;
  int32 Index = 0;
  for (const auto &Mesh : Meshes) {
    if (!Mesh->GetVertices().size())
    {
      continue;
    }
    AProceduralMeshActor* TempActor = GetWorld()->SpawnActor<AProceduralMeshActor>();
    UProceduralMeshComponent *TempPMC = TempActor->MeshComponent;
    TempPMC->bUseAsyncCooking = true;
    TempPMC->bUseComplexAsSimpleCollision = true;
    TempPMC->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

    const FProceduralCustomMesh MeshData = *Mesh;
    TempPMC->CreateMeshSection_LinearColor(
        0,
        MeshData.Vertices,
        MeshData.Triangles,
        MeshData.Normals,
        TArray<FVector2D>(), // UV0
        TArray<FLinearColor>(), // VertexColor
        TArray<FProcMeshTangent>(), // Tangents
        true); // Create collision
    ActorMeshList.Add(TempActor);
    UStaticMesh* GeneratedMesh = CreateStaticMeshAsset(TempPMC, Index);
    if( GeneratedMesh != nullptr)
    {
      MeshesToSpawn.Add(GeneratedMesh);
    }
    Index++;
  }

  for( auto CurrentActor : ActorMeshList )
  {
    CurrentActor->Destroy();
  }
  for(auto CurrentMesh : MeshesToSpawn )
  {
    AStaticMeshActor* TempActor = GetWorld()->SpawnActor<AStaticMeshActor>();
    TempActor->GetStaticMeshComponent()->SetStaticMesh(CurrentMesh);
  }
/*
  if(!Parameters.enable_mesh_visibility)
  {
    for(AActor * actor : ActorMeshList)
    {
      actor->SetActorHiddenInGame(true);
    }
  }
*/
  // // Build collision data
  // FTriMeshCollisionData CollisitonData;
  // CollisitonData.bDeformableMesh = false;
  // CollisitonData.bDisableActiveEdgePrecompute = false;
  // CollisitonData.bFastCook = false;
  // CollisitonData.bFlipNormals = false;
  // CollisitonData.Indices = TriIndices;
  // CollisitonData.Vertices = Vertices;

  // RoadMesh->ContainsPhysicsTriMeshData(true);
  // bool Success = RoadMesh->GetPhysicsTriMeshData(&CollisitonData, true);
  // if (!Success)
  // {
  //   UE_LOG(LogCarla, Error, TEXT("The road collision mesh could not be generated!"));
  // }
}

void UOpenDriveToMap::GenerateSpawnPoints( const boost::optional<carla::road::Map>& CarlaMap )
{
  float SpawnersHeight = 300.f;
  const auto Waypoints = CarlaMap->GenerateWaypointsOnRoadEntries();
  for (const auto &Wp : Waypoints)
  {
    const FTransform Trans = CarlaMap->ComputeTransform(Wp);
    AVehicleSpawnPoint *Spawner = GetWorld()->SpawnActor<AVehicleSpawnPoint>();
    Spawner->SetActorRotation(Trans.GetRotation());
    Spawner->SetActorLocation(Trans.GetTranslation() + FVector(0.f, 0.f, SpawnersHeight));
    //VehicleSpawners.Add(Spawner);
  }
}

UStaticMesh* UOpenDriveToMap::CreateStaticMeshAsset(UProceduralMeshComponent* ProcMeshComp, int32 MeshIndex)
{
  FMeshDescription MeshDescription = BuildMeshDescription(ProcMeshComp);

  IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

  // If we got some valid data.
  if (MeshDescription.Polygons().Num() > 0)
  {
    FString MeshName = *(FString("Build") + FString::FromInt(MeshIndex) );
    FString PackageName = "/Game/CustomMaps/" + MapName + "/Static/" + MeshName;
    if( !PlatformFile.DirectoryExists(*PackageName) )
    {
      PlatformFile.CreateDirectory(*PackageName);
    }
    UE_LOG(LogCarlaToolsMapGenerator, Log, TEXT("PackageName %s"), *PackageName );

    // Then find/create it.
    UPackage* Package = CreatePackage(*PackageName);
    check(Package);
    // Create StaticMesh object
    UStaticMesh* StaticMesh = NewObject<UStaticMesh>(Package, *MeshName, RF_Public | RF_Standalone);
    StaticMesh->InitResources();

    StaticMesh->LightingGuid = FGuid::NewGuid();

    // Add source to new StaticMesh
    FStaticMeshSourceModel& SrcModel = StaticMesh->AddSourceModel();
    SrcModel.BuildSettings.bRecomputeNormals = false;
    SrcModel.BuildSettings.bRecomputeTangents = false;
    SrcModel.BuildSettings.bRemoveDegenerates = false;
    SrcModel.BuildSettings.bUseHighPrecisionTangentBasis = false;
    SrcModel.BuildSettings.bUseFullPrecisionUVs = false;
    SrcModel.BuildSettings.bGenerateLightmapUVs = true;
    SrcModel.BuildSettings.SrcLightmapIndex = 0;
    SrcModel.BuildSettings.DstLightmapIndex = 1;
    StaticMesh->CreateMeshDescription(0, MoveTemp(MeshDescription));
    StaticMesh->CommitMeshDescription(0);

    //// SIMPLE COLLISION
    if (!ProcMeshComp->bUseComplexAsSimpleCollision )
    {
      StaticMesh->CreateBodySetup();
      UBodySetup* NewBodySetup = StaticMesh->BodySetup;
      NewBodySetup->BodySetupGuid = FGuid::NewGuid();
      NewBodySetup->AggGeom.ConvexElems = ProcMeshComp->ProcMeshBodySetup->AggGeom.ConvexElems;
      NewBodySetup->bGenerateMirroredCollision = false;
      NewBodySetup->bDoubleSidedGeometry = true;
      NewBodySetup->CollisionTraceFlag = CTF_UseDefault;
      NewBodySetup->CreatePhysicsMeshes();
    }

    //// MATERIALS
    TSet<UMaterialInterface*> UniqueMaterials;
    const int32 NumSections = ProcMeshComp->GetNumSections();
    for (int32 SectionIdx = 0; SectionIdx < NumSections; SectionIdx++)
    {
      FProcMeshSection *ProcSection =
        ProcMeshComp->GetProcMeshSection(SectionIdx);
      UMaterialInterface *Material = ProcMeshComp->GetMaterial(SectionIdx);
      UniqueMaterials.Add(Material);
    }
    // Copy materials to new mesh
    for (auto* Material : UniqueMaterials)
    {
      StaticMesh->StaticMaterials.Add(FStaticMaterial(Material));
    }

    //Set the Imported version before calling the build
    StaticMesh->ImportVersion = EImportStaticMeshVersion::LastVersion;

    // Build mesh from source
    StaticMesh->Build(false);
    StaticMesh->PostEditChange();

    // Notify asset registry of new asset
    FAssetRegistryModule::AssetCreated(StaticMesh);
    return StaticMesh;
  }
  return nullptr;
}