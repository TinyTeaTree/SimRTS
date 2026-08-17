#include "SimRTSSoldierActor.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// BasicShapes Cylinder: radius 50 UU, height 100 UU at scale 1.
	// Scale Z×2 → 2 m tall selection volume; center elevated so feet sit on Z=0.
	constexpr float kSoldierScaleXY = 1.f;
	constexpr float kSoldierScaleZ = 2.f;
	constexpr float kBasicCylinderHalfHeightUU = 50.f;
}

ASimRTSSoldierActor::ASimRTSSoldierActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CylinderMesh.Object);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ASimRTSSoldierActor: failed to load BasicShapes Cylinder mesh"));
	}

	// Keep the cylinder for selection traces; hide the placeholder visual.
	MeshComponent->SetRelativeScale3D(FVector(kSoldierScaleXY, kSoldierScaleXY, kSoldierScaleZ));
	MeshComponent->SetRelativeLocation(FVector(0.f, 0.f, kBasicCylinderHalfHeightUU * kSoldierScaleZ));
	MeshComponent->SetHiddenInGame(true);
	MeshComponent->SetVisibility(false);
	MeshComponent->SetCastShadow(false);

	CharacterMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CharacterMesh"));
	CharacterMesh->SetupAttachment(SceneRoot);
	CharacterMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CharacterMesh->SetGenerateOverlapEvents(false);
	CharacterMesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	CharacterMesh->SetHiddenInGame(false);
	CharacterMesh->SetVisibility(true);

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> MannyMesh(
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (MannyMesh.Succeeded())
	{
		MannequinMesh = MannyMesh.Object;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ASimRTSSoldierActor: failed to load SKM_Manny_Simple — open the editor once to cook/load Content/Characters/Mannequins"));
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> IdleFinder(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	if (IdleFinder.Succeeded())
	{
		IdleAnim = IdleFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> RunFinder(
		TEXT("/Game/Characters/Mannequins/Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd.MF_Unarmed_Jog_Fwd"));
	if (RunFinder.Succeeded())
	{
		RunAnim = RunFinder.Object;
	}

	static ConstructorHelpers::FObjectFinder<UAnimSequence> PushedFinder(
		TEXT("/Game/Characters/Mannequins/Anims/Rifle/HitReact/MM_HitReact_Front_Lgt_01.MM_HitReact_Front_Lgt_01"));
	if (PushedFinder.Succeeded())
	{
		PushedAnim = PushedFinder.Object;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ASimRTSSoldierActor: failed to load MM_HitReact_Front_Lgt_01"));
	}

	ApplyCharacterDefaults();
}

void ASimRTSSoldierActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyCharacterDefaults();
}

void ASimRTSSoldierActor::ApplyCharacterDefaults()
{
	if (CharacterMesh == nullptr)
	{
		return;
	}

	// Absolute world pose only while smoothing; otherwise follow the actor/cylinder.
	CharacterMesh->SetUsingAbsoluteLocation(bSmoothVisualPose);
	CharacterMesh->SetUsingAbsoluteRotation(bSmoothVisualPose);
	if (!bSmoothVisualPose)
	{
		CharacterMesh->SetRelativeLocation(FVector::ZeroVector);
		CharacterMesh->SetRelativeRotation(FRotator(0.f, MeshYawOffsetDegrees, 0.f));
	}

	if (MannequinMesh != nullptr && CharacterMesh->GetSkeletalMeshAsset() != MannequinMesh)
	{
		CharacterMesh->SetSkeletalMesh(MannequinMesh);
	}

	CharacterMesh->SetVisibility(true);
	CharacterMesh->SetHiddenInGame(false);
}

void ASimRTSSoldierActor::BeginPlay()
{
	Super::BeginPlay();
	PrimaryActorTick.bCanEverTick = true;
	SetActorTickEnabled(true);
	ApplyCharacterDefaults();
	PlayLocomotion(false);
}

void ASimRTSSoldierActor::SyncWorldPose(const FVector& GroundLocation, float YawDegrees, bool bIsMoving)
{
	// Cylinder / arrow / actor stay on the discrete sim pose.
	Super::SyncWorldPose(GroundLocation, YawDegrees, bIsMoving);

	VisualTargetLocation = GetActorLocation();
	VisualTargetYawDegrees = YawDegrees;

	// Keep absolute/relative mode in sync if the flag was changed at runtime.
	if (CharacterMesh != nullptr)
	{
		CharacterMesh->SetUsingAbsoluteLocation(bSmoothVisualPose);
		CharacterMesh->SetUsingAbsoluteRotation(bSmoothVisualPose);
	}

	if (!bVisualPoseInitialized || !bSmoothVisualPose)
	{
		SnapVisualToTarget();
		bVisualPoseInitialized = true;
	}
}

void ASimRTSSoldierActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bSmoothVisualPose || !bVisualPoseInitialized || CharacterMesh == nullptr)
	{
		return;
	}

	const FVector NewLocation = FMath::VInterpTo(
		CharacterMesh->GetComponentLocation(),
		VisualTargetLocation,
		DeltaSeconds,
		VisualLocationInterpSpeed);
	CharacterMesh->SetWorldLocation(NewLocation);

	const FRotator CurrentRot(0.f, CharacterMesh->GetComponentRotation().Yaw, 0.f);
	const FRotator TargetRot(0.f, VisualTargetYawDegrees + MeshYawOffsetDegrees, 0.f);
	CharacterMesh->SetWorldRotation(FMath::RInterpTo(CurrentRot, TargetRot, DeltaSeconds, VisualRotationInterpSpeed));
}

void ASimRTSSoldierActor::SnapVisualToTarget()
{
	if (CharacterMesh == nullptr)
	{
		return;
	}

	if (bSmoothVisualPose)
	{
		CharacterMesh->SetWorldLocation(VisualTargetLocation);
		CharacterMesh->SetWorldRotation(FRotator(0.f, VisualTargetYawDegrees + MeshYawOffsetDegrees, 0.f));
	}
	else
	{
		CharacterMesh->SetRelativeLocation(FVector::ZeroVector);
		CharacterMesh->SetRelativeRotation(FRotator(0.f, MeshYawOffsetDegrees, 0.f));
	}
}

float ASimRTSSoldierActor::GetPivotHeight() const
{
	// Character root/feet at ground.
	return 0.f;
}

void ASimRTSSoldierActor::OnMovingChanged(bool bIsMoving)
{
	if (bPushAnimLocked)
	{
		return;
	}

	PlayLocomotion(bIsMoving);
}

void ASimRTSSoldierActor::NotifyPinnedPush()
{
	PlayPushedAnim();
}

void ASimRTSSoldierActor::PlayLocomotion(bool bIsMoving)
{
	if (bPushAnimLocked || CharacterMesh == nullptr)
	{
		return;
	}

	UAnimSequence* Anim = bIsMoving ? RunAnim.Get() : IdleAnim.Get();
	if (Anim == nullptr)
	{
		return;
	}

	CharacterMesh->PlayAnimation(Anim, /*bLooping=*/true);
}

void ASimRTSSoldierActor::PlayPushedAnim()
{
	if (bPushAnimLocked || CharacterMesh == nullptr)
	{
		return;
	}

	UAnimSequence* Anim = PushedAnim.Get();
	if (Anim == nullptr)
	{
		return;
	}

	CharacterMesh->PlayAnimation(Anim, /*bLooping=*/false);
	bPushAnimLocked = true;

	const float Duration = FMath::Max(Anim->GetPlayLength(), 0.05f);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			PushAnimTimer,
			this,
			&ASimRTSSoldierActor::OnPushAnimFinished,
			Duration,
			false);
	}
	else
	{
		OnPushAnimFinished();
	}
}

void ASimRTSSoldierActor::OnPushAnimFinished()
{
	bPushAnimLocked = false;
	PushAnimTimer.Invalidate();
	PlayLocomotion(bMoving);
}
