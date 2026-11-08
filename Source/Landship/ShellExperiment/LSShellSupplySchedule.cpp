#include "ShellExperiment/LSShellSupplySchedule.h"

#include "ShellExperiment/LSShellExperimentCoordinator.h"
#include "ShellExperiment/LSShellExperimentEnemy.h"
#include "ShellExperiment/LSShellSiegeController.h"
#include "ShellExperiment/LSShellReactiveComponent.h"
#include "Castle/LSCastle.h"
#include "Enemies/LSOutpost.h"
#include "Combat/LSCollisionChannels.h"
#include "Combat/LSCombatTargetLibrary.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Components/TextRenderComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "Maps/LSBattlefieldSettings.h"

ALSShellSupplySchedule::ALSShellSupplySchedule()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ScheduleRoot")));
	TransportClass = ALSShellTransport::StaticClass();
	EnemyClass = ALSShellExperimentEnemy::StaticClass();
	RouteA.CargoSlots = GetDefault<ALSShellTransport>()->CargoSlots;
	RouteB.Start = FVector(-900, 1000, 70);
	RouteB.End = FVector(900, 0, 70);
	RouteB.FirstDepartureSeconds = 16;
	RouteB.RepeatSeconds = 36;
	RouteB.ReplenishmentOffset = FVector(0, 700, 0);
	RouteB.UnloadOffsets = { FVector::ZeroVector, FVector(0, 1200, 0), FVector(0, -1250, 0) };
	for (int32 Index = 0; Index < 2; ++Index)
	{
		auto* Label = CreateDefaultSubobject<UTextRenderComponent>(*FString::Printf(TEXT("RouteLabel%d"), Index));
		Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Label->SetCanEverAffectNavigation(false);
		Label->SetupAttachment(GetRootComponent());
		Label->SetAbsolute(true, true, true);
		Label->SetWorldRotation(FRotator(55, 90, 0));
		Label->SetHorizontalAlignment(EHTA_Center);
		Label->SetWorldSize(28);
		RouteLabels.Add(Label);
	}
}

void ALSShellSupplySchedule::BeginPlay()
{
	Super::BeginPlay();
	ConfigurationError = ValidateConfiguration();
	NextDepartures[0] = RouteA.FirstDepartureSeconds;
	NextDepartures[1] = RouteB.FirstDepartureSeconds;
	if (!ConfigurationError.IsEmpty()) UE_LOG(LogTemp, Error, TEXT("%s: %s"), *GetName(), *ConfigurationError);
	if (ConfigurationError.IsEmpty() && bArenaPressureEnabled)
	{
		UStaticMesh* MarkMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
		for (const FVector& Entry : ArenaPressureEntries)
		{
			auto* Mark = NewObject<UStaticMeshComponent>(this);
			Mark->SetStaticMesh(MarkMesh);
			Mark->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mark->SetCanEverAffectNavigation(false);
			Mark->SetupAttachment(GetRootComponent());
			Mark->RegisterComponent();
			Mark->SetWorldLocation(Entry - FVector(0, 0, 85));
			Mark->SetWorldScale3D(FVector(1.2, 1.2, .03));
			PressureMarks.Add(Mark);
			auto* Label = NewObject<UTextRenderComponent>(this);
			Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Label->SetCanEverAffectNavigation(false);
			Label->SetupAttachment(GetRootComponent());
			Label->RegisterComponent();
			Label->SetWorldLocation(Entry + FVector(0, 0, 180));
			Label->SetWorldRotation(FRotator(55, 90, 0));
			Label->SetHorizontalAlignment(EHTA_Center);
			Label->SetWorldSize(28);
			Label->SetVisibility(false);
			PressureLabels.Add(Label);
		}
	}
}

float ALSShellSupplySchedule::GetRequiredWarningSeconds() const
{
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	return Coordinator ? FMath::Max(8.f, Coordinator->Tuning.ReloadSeconds + WorstObservedShotFlightSeconds + Coordinator->GetMaximumFollowupDelay() + InputMarginSeconds) : 8.f;
}

FString ALSShellSupplySchedule::ValidateConfiguration() const
{
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsSupplyExperiment()) return TEXT("보급 일정에는 개선 실험 조정자가 필요하다.");
	if (!TransportClass || TransportClass->HasAnyClassFlags(CLASS_Abstract) || !EnemyClass || EnemyClass->HasAnyClassFlags(CLASS_Abstract) || !EnemyStatsTable)
		return TEXT("보급 차량·병력 클래스와 공통 병력 수치 표가 필요하다.");
	if (MaximumActiveDeliveriesPerRoute != 3 || !FMath::IsFinite(WarningSeconds) || !FMath::IsFinite(WorstObservedShotFlightSeconds) ||
		WorstObservedShotFlightSeconds < 0 || !FMath::IsFinite(InputMarginSeconds) || InputMarginSeconds < 0 ||
		WarningSeconds < GetRequiredWarningSeconds() || !FMath::IsFinite(NoticeSeconds) || NoticeSeconds <= 0)
		return TEXT("경로마다 잔존 보급은 3회분 이하이며 예고는 실제 재장전·비행·효과 지연·입력 여유를 포함해야 한다.");
	if (!FMath::IsFinite(ReplenishmentRetrySeconds) || ReplenishmentRetrySeconds <= 0 || MaximumReplenishmentAttempts < 1 || MaximumReplenishmentAttempts > 32)
		return TEXT("병력 하역 재시도는 유한한 양수 간격과 1~32회 한도가 필요하다.");
	if (!IsValid(InitialRouteATransport) || InitialRouteATransport->GetWorld() != GetWorld() || InitialAssaults.IsEmpty() ||
		!IsValid(InitialHeavy) || !InitialHeavy->IsHeavy() || InitialHeavy->GetWorld() != GetWorld()) return TEXT("초기 A 차량 하나, Assault 목록과 Heavy 하나를 명시해야 한다.");
	if (!InitialRouteATransport->RouteStart.Equals(RouteA.Start, UE_KINDA_SMALL_NUMBER) ||
		!InitialRouteATransport->RouteEnd.Equals(RouteA.End, UE_KINDA_SMALL_NUMBER)) return TEXT("초기 A 차량과 보급 일정의 실제 경로가 다르다.");
	TSet<const ALSShellExperimentEnemy*> UniqueAssaults;
	for (const ALSShellExperimentEnemy* Assault : InitialAssaults)
	{
		if (!IsValid(Assault) || Assault->EnemyKind != ELSShellEnemyKind::Assault || Assault->GetWorld() != GetWorld() || UniqueAssaults.Contains(Assault))
			return TEXT("초기 Assault는 같은 월드의 유효한 서로 다른 액터여야 한다.");
		UniqueAssaults.Add(Assault);
	}
	if (bArenaPressureEnabled)
	{
		if (!Coordinator->IsArenaExperiment() || ArenaPressureTimes.IsEmpty() || ArenaPressureEntries.IsEmpty() || ArenaPressureEnemyCap < 1 ||
			!FMath::IsFinite(ArenaPressureRetrySeconds) || ArenaPressureRetrySeconds <= 0 || ArenaPressureMaximumAttempts < 1 ||
			!FMath::IsFinite(ArenaPressureAlternateOffset) || ArenaPressureAlternateOffset <= 0) return TEXT("측면 증원 설정이 유효하지 않다.");
		float Previous = -1;
		for (float Time : ArenaPressureTimes)
		{
			if (!FMath::IsFinite(Time) || Time < WarningSeconds || Time <= Previous) return TEXT("측면 증원 시간은 예고 시간 이후의 오름차순이어야 한다.");
			Previous = Time;
		}
		for (const FVector& Entry : ArenaPressureEntries)
			if (Entry.ContainsNaN() || FMath::IsNearlyZero(Entry.Y)) return TEXT("측면 증원 위치가 유효하지 않다.");
	}
	const auto* InitialSiege = InitialHeavy->FindComponentByClass<ULSShellSiegeController>();
	if (!InitialSiege || InitialSiege->InitialLoadSeconds < GetRequiredWarningSeconds())
		return TEXT("초기 Heavy에도 실제 반응 시간을 확보한 공성 제어가 필요하다.");
	const FLSShellSupplyRoute* Routes[] { &RouteA, &RouteB };
	for (const FLSShellSupplyRoute* Route : Routes)
	{
		if (Route->Start.ContainsNaN() || Route->End.ContainsNaN() || Route->ReplenishmentOffset.ContainsNaN() ||
			Route->SiegeDeploymentLocation.ContainsNaN() || FVector::DistSquaredXY(Route->Start, Route->End) <= UE_KINDA_SMALL_NUMBER ||
			!FMath::IsFinite(Route->FirstDepartureSeconds) || Route->FirstDepartureSeconds < 0 ||
			!FMath::IsFinite(Route->RepeatSeconds) || Route->RepeatSeconds <= 0 || Route->CargoSlots.IsEmpty()) return TEXT("보급 경로·간격·실제 화물 구성이 유효하지 않다.");
		if (Route->UnloadOffsets.Num() != 3) return TEXT("각 경로에는 서로 다른 하역 후보 세 곳이 필요하다.");
		for (int32 Index = 0; Index < Route->UnloadOffsets.Num(); ++Index)
		{
			if (Route->UnloadOffsets[Index].ContainsNaN()) return TEXT("하역 후보는 유한한 월드 오프셋이어야 한다.");
			for (int32 Previous = 0; Previous < Index; ++Previous)
				if (Route->UnloadOffsets[Index].Equals(Route->UnloadOffsets[Previous], UE_KINDA_SMALL_NUMBER)) return TEXT("하역 후보는 서로 달라야 한다.");
		}
		for (const FVector& Waypoint : Route->ExitWaypoints) if (Waypoint.ContainsNaN()) return TEXT("퇴장 경유점은 유한해야 한다.");
		for (const TArray<FLSShellCargoSlot>* Slots : { &Route->CargoSlots, &Route->ChargedCargoSlots })
			for (const FLSShellCargoSlot& Slot : *Slots)
				if (!Slot.CargoClass || Slot.CargoClass->HasAnyClassFlags(CLASS_Abstract) || !Slot.CargoClass.GetDefaultObject()->GetPhysicalBody() ||
					Slot.LocalTransform.ContainsNaN() || !Slot.LocalTransform.GetScale3D().Equals(FVector::OneVector, UE_KINDA_SMALL_NUMBER) ||
					!FMath::IsFinite(Slot.MassOverrideKg) || Slot.MassOverrideKg < 0) return TEXT("보급 화물은 실제 물리 몸체를 가진 유효한 슬롯이어야 한다.");
	}
	if (RouteB.ChargedCargoSlots.IsEmpty()) return TEXT("B 경로 두 번째 회차의 실제 충전 화물을 지정해야 한다.");
	if (RouteA.FirstDepartureSeconds != 0 || RouteB.FirstDepartureSeconds < WarningSeconds ||
		FVector::DistXY(RouteA.Start, RouteA.End) / InitialRouteATransport->ForwardSpeed < WarningSeconds)
		return TEXT("초기 보급은 위협이 도달하기 전 필요한 예고 시간을 확보해야 한다.");
	return FString();
}

int32 ALSShellSupplySchedule::GetScheduledDepartureOrdinal(const FLSShellSupplyRoute& Route, double ElapsedSeconds)
{
	if (!FMath::IsFinite(ElapsedSeconds) || !FMath::IsFinite(Route.FirstDepartureSeconds) || !FMath::IsFinite(Route.RepeatSeconds) ||
		Route.RepeatSeconds <= 0 || ElapsedSeconds < Route.FirstDepartureSeconds) return 0;
	return FMath::FloorToInt((ElapsedSeconds - Route.FirstDepartureSeconds) / Route.RepeatSeconds) + 1;
}

bool ALSShellSupplySchedule::HasRemainingResources(const FLSActiveDelivery& Delivery)
{
	if (Delivery.Vehicle.IsValid() && !Delivery.Vehicle->IsActorBeingDestroyed()) return true;
	for (const TWeakObjectPtr<AActor>& Resource : Delivery.Resources)
		if (Resource.IsValid() && !Resource->IsActorBeingDestroyed()) return true;
	return false;
}

int32 ALSShellSupplySchedule::GetActiveDeliveryCount(int32 RouteIndex) const
{
	int32 Count = 0;
	for (const FLSActiveDelivery& Delivery : Deliveries)
		if (Delivery.RouteIndex == RouteIndex && HasRemainingResources(Delivery)) ++Count;
	return Count;
}

void ALSShellSupplySchedule::NotifyDeliveryActorReplaced(AActor* Original, AActor* Replacement)
{
	if (!Original || !IsValid(Replacement) || Replacement->GetWorld() != GetWorld()) return;
	for (FLSActiveDelivery& Delivery : Deliveries)
	{
		if (Delivery.Vehicle.Get() == Original) Delivery.Resources.AddUnique(Replacement);
		for (TWeakObjectPtr<AActor>& Resource : Delivery.Resources) if (Resource.Get() == Original) Resource = Replacement;
	}
}

bool ALSShellSupplySchedule::IsLoadedPoseClear(FVector Position, const FLSShellSupplyRoute& Route, const TArray<FLSShellCargoSlot>& Cargo) const
{
	const FTransform ChassisTransform((Route.End - Route.Start).Rotation(), Position);
	const auto* VehicleDefaults = TransportClass.GetDefaultObject();
	const ALSBattlefieldSettings* Battlefield = ALSBattlefieldSettings::Find(GetWorld());
	if (!VehicleDefaults || !Battlefield) return false;
	TArray<TPair<const UPrimitiveComponent*, FTransform>> Shapes;
	Shapes.Emplace(VehicleDefaults->GetPhysicalBody(), ChassisTransform);
	for (const FLSShellCargoSlot& Slot : Cargo)
	{
		const ALSShellPhysicalProp* Defaults = Slot.CargoClass.GetDefaultObject();
		if (!Defaults || !Defaults->GetPhysicalBody()) return false;
		Shapes.Emplace(Defaults->GetPhysicalBody(), Slot.LocalTransform * ChassisTransform);
	}
	const FBox2D Bounds = Battlefield->GetWorldBounds();
	for (const auto& Shape : Shapes)
	{
		const FBox BodyBounds = Shape.Key->CalcBounds(Shape.Value).GetBox();
		if (BodyBounds.Min.X < Bounds.Min.X || BodyBounds.Max.X > Bounds.Max.X || BodyBounds.Min.Y < Bounds.Min.Y || BodyBounds.Max.Y > Bounds.Max.Y) return false;
		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(Overlaps, Shape.Value.GetLocation(), Shape.Value.GetRotation(),
			FCollisionObjectQueryParams::AllObjects, Shape.Key->GetCollisionShape(-UE_KINDA_SMALL_NUMBER),
			FCollisionQueryParams(SCENE_QUERY_STAT(ShellSupplySlotClearance), false, this));
		for (const FOverlapResult& Overlap : Overlaps)
			if (Overlap.Component.IsValid() && Overlap.Component->GetCollisionResponseToChannel(Shape.Key->GetCollisionObjectType()) == ECR_Block &&
				Shape.Key->GetCollisionResponseToChannel(Overlap.Component->GetCollisionObjectType()) == ECR_Block) return false;
	}
	return true;
}

bool ALSShellSupplySchedule::HasGroundAlongSegment(FVector Start, FVector End, float BodyHalfHeight) const
{
	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(FVector::DistXY(Start, End) / 100));
	for (int32 Step = 0; Step <= Steps; ++Step)
	{
		const FVector Point = FMath::Lerp(Start, End, static_cast<float>(Step) / Steps);
		FHitResult Ground;
		if (!GetWorld()->LineTraceSingleByObjectType(Ground, Point + FVector(0, 0, 100), Point - FVector(0, 0, BodyHalfHeight + 10), GroundObjects) ||
			FMath::Abs(Ground.ImpactPoint.Z - (Point.Z - BodyHalfHeight)) > 5 || Ground.ImpactNormal.Z < .99f) return false;
	}
	return true;
}

bool ALSShellSupplySchedule::IsLoadedRouteClear(const FLSShellSupplyRoute& Route, const TArray<FLSShellCargoSlot>& Cargo) const
{
	const FQuat Heading = (Route.End - Route.Start).Rotation().Quaternion();
	const ALSShellTransport* Defaults = TransportClass.GetDefaultObject();
	if (!Defaults || !HasGroundAlongSegment(Route.Start, Route.End, Defaults->GetPhysicalBody()->CalcBounds(FTransform::Identity).BoxExtent.Z)) return false;
	TArray<TPair<const UPrimitiveComponent*, FTransform>> Shapes;
	Shapes.Emplace(Defaults->GetPhysicalBody(), FTransform::Identity);
	for (const FLSShellCargoSlot& Slot : Cargo) Shapes.Emplace(Slot.CargoClass.GetDefaultObject()->GetPhysicalBody(), Slot.LocalTransform);
	for (const auto& Shape : Shapes)
	{
		const FTransform Start = Shape.Value * FTransform(Heading, Route.Start);
		const FTransform End = Shape.Value * FTransform(Heading, Route.End);
		TArray<FHitResult> Hits;
		GetWorld()->SweepMultiByObjectType(Hits, Start.GetLocation(), End.GetLocation(), Start.GetRotation(), FCollisionObjectQueryParams::AllObjects,
			Shape.Key->GetCollisionShape(-UE_KINDA_SMALL_NUMBER), FCollisionQueryParams(SCENE_QUERY_STAT(ShellSupplyLoadedRoute), false, this));
		for (const FHitResult& Hit : Hits)
			if (const UPrimitiveComponent* Other = Hit.GetComponent(); Other && Other->GetCollisionObjectType() != LSCollisionChannels::Ground &&
				Other->GetCollisionResponseToChannel(Shape.Key->GetCollisionObjectType()) == ECR_Block &&
				Shape.Key->GetCollisionResponseToChannel(Other->GetCollisionObjectType()) == ECR_Block) return false;
	}
	return true;
}

bool ALSShellSupplySchedule::FindAvailableDepartureRoute(int32 RouteIndex, FLSShellSupplyRoute& OutRoute) const
{
	const FLSShellSupplyRoute& AuthoredRoute = RouteIndex == 0 ? RouteA : RouteB;
	OutRoute = AuthoredRoute;
	const bool bCharged = RouteIndex == 1 && SuccessfulDepartures[RouteIndex] + 1 == 2;
	const TArray<FLSShellCargoSlot>& Slots = bCharged ? OutRoute.ChargedCargoSlots : OutRoute.CargoSlots;
	for (const FVector& Offset : AuthoredRoute.UnloadOffsets)
	{
		OutRoute.End = AuthoredRoute.End + Offset;
		if (IsLoadedPoseClear(OutRoute.End, OutRoute, Slots) && IsLoadedPoseClear(OutRoute.Start, OutRoute, Slots) && IsLoadedRouteClear(OutRoute, Slots)) return true;
	}
	return false;
}

bool ALSShellSupplySchedule::IsExitRouteClear(ALSShellTransport* Vehicle, const TArray<FVector>& Waypoints) const
{
	const TArray<UPrimitiveComponent*> Bodies = Vehicle->GetAssemblyBodies();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShellSupplyExitRoute), false, this);
	for (const UPrimitiveComponent* Body : Bodies) Params.AddIgnoredActor(Body->GetOwner());
	FVector Start = Vehicle->GetActorLocation();
	FQuat Heading = Vehicle->GetActorQuat();
	bool bFirstSegment = true;
	for (const FVector& End : Waypoints)
	{
		if (!HasGroundAlongSegment(Start, End, Vehicle->GetPhysicalBody()->CalcBounds(Vehicle->GetActorTransform()).BoxExtent.Z)) return false;
		const FQuat EndHeading = (End - Start).Rotation().Quaternion();
		FCollisionQueryParams SegmentParams = Params;
		if (bFirstSegment)
			for (ALSShellPhysicalProp* Cargo : Vehicle->GetCargoActors())
			{
				FVector DeckPoint, CargoPoint;
				const float DeckDistance = Vehicle->GetPhysicalBody()->GetClosestPointOnCollision(Cargo->GetPhysicalBody()->GetCenterOfMass(), DeckPoint);
				const float Gap = Cargo->GetPhysicalBody()->GetClosestPointOnCollision(DeckPoint, CargoPoint);
				if (DeckDistance >= 0 && Gap >= 0 && Gap <= 3 && Cargo->GetPhysicalBody()->GetCenterOfMass().Z > DeckPoint.Z &&
					DeckPoint.Z >= Vehicle->GetPhysicalBody()->Bounds.GetBox().Max.Z - 3 &&
					Vehicle->Reactive->GetContinuingContactEpoch(Cargo->GetPhysicalBody()) != 0)
					// 첫 구간은 기존 지지 접점에서 빠져나간다. 실제 몸체 사이의 솔버 충돌은 계속 켜 둔다.
					SegmentParams.AddIgnoredActor(Cargo);
			}
		const int32 AngleSteps = FMath::Max(1, FMath::CeilToInt(FMath::RadiansToDegrees(Heading.AngularDistance(EndHeading)) / 5));
		for (int32 AngleStep = 0; AngleStep <= AngleSteps; ++AngleStep)
		{
			const FQuat SweepHeading = FQuat::Slerp(Heading, EndHeading, static_cast<float>(AngleStep) / AngleSteps);
			for (const UPrimitiveComponent* Body : Bodies)
			{
				const FTransform Relative = Body->GetComponentTransform().GetRelativeTransform(Vehicle->GetActorTransform());
				const FTransform BodyStart = Relative * FTransform(SweepHeading, Start), BodyEnd = Relative * FTransform(SweepHeading, End);
				TArray<FHitResult> Hits;
				GetWorld()->SweepMultiByObjectType(Hits, BodyStart.GetLocation(), BodyEnd.GetLocation(), BodyStart.GetRotation(),
					FCollisionObjectQueryParams::AllObjects, Body->GetCollisionShape(-UE_KINDA_SMALL_NUMBER), SegmentParams);
				for (const FHitResult& Hit : Hits)
					if (const UPrimitiveComponent* Other = Hit.GetComponent(); Other && Other->GetCollisionObjectType() != LSCollisionChannels::Ground &&
						Other->GetCollisionResponseToChannel(Body->GetCollisionObjectType()) == ECR_Block &&
						Body->GetCollisionResponseToChannel(Other->GetCollisionObjectType()) == ECR_Block) return false;
			}
		}
		Start = End;
		Heading = EndHeading;
		bFirstSegment = false;
	}
	return true;
}

TArray<FVector> ALSShellSupplySchedule::BuildExitWaypoints(const ALSShellTransport* Vehicle, int32 RouteIndex, FVector UnloadLocation) const
{
	const FLSShellSupplyRoute& Route = RouteIndex == 0 ? RouteA : RouteB;
	TArray<FVector> Waypoints = Route.ExitWaypoints;
	if (Waypoints.IsEmpty())
	{
		const auto* Battlefield = ALSBattlefieldSettings::Find(GetWorld());
		const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
		const UPrimitiveComponent* CoreBody = Coordinator && Coordinator->CoreOutpost ? Cast<UPrimitiveComponent>(Coordinator->CoreOutpost->GetRootComponent()) : nullptr;
		if (!Battlefield || !CoreBody) return {};
		const FBox2D Bounds = Battlefield->GetWorldBounds();
		const float Radius = Vehicle->GetPhysicalBody()->CalcBounds(FTransform::Identity).BoxExtent.Size2D();
		const float TurnX = CoreBody->Bounds.GetBox().Min.X - Radius - 50;
		const float LaneY = RouteIndex == 0 ? Bounds.Min.Y + 200 : Bounds.Max.Y - 200;
		const float Z = Vehicle->GetActorLocation().Z;
		Waypoints = { FVector(TurnX, UnloadLocation.Y, Z), FVector(TurnX, LaneY, Z), FVector(Bounds.Max.X + 600, LaneY, Z) };
	}
	TArray<FVector> DistinctWaypoints;
	FVector Previous = UnloadLocation;
	for (const FVector& Point : Waypoints)
		if (FVector::DistXY(Previous, Point) > Vehicle->ArrivalDistance) { DistinctWaypoints.Add(Point); Previous = Point; }
	return DistinctWaypoints;
}

void ALSShellSupplySchedule::TryExit(FLSActiveDelivery& Delivery)
{
	ALSShellTransport* Vehicle = Delivery.Vehicle.Get();
	if (!Vehicle || Delivery.bExitStarted || !Delivery.bReplenishmentHandled || Vehicle->HasRackedCargo() || !Vehicle->HasDeliveredCargo() || !ULSCombatTargetLibrary::IsCombatAlive(Vehicle) ||
		GetWorld()->GetTimeSeconds() < Delivery.NextExitAttemptSeconds) return;
	Delivery.NextExitAttemptSeconds = GetWorld()->GetTimeSeconds() + 1;
	const TArray<FVector> Waypoints = BuildExitWaypoints(Vehicle, Delivery.RouteIndex, Delivery.UnloadLocation);
	if (!Waypoints.IsEmpty() && IsExitRouteClear(Vehicle, Waypoints) && Vehicle->StartExitRoute(Waypoints))
	{
		Delivery.bExitStarted = true;
		Delivery.bExitBlocked = false;
		SetNotice(Delivery.RouteIndex, TEXT("하역 완료 · 빈 수송차가 실제 출구로 이동"));
	}
	else if (!Delivery.bExitBlocked)
	{
		Delivery.bExitBlocked = true;
		SetNotice(Delivery.RouteIndex, TEXT("퇴장 대기 · 실제 출구 경로 점유 또는 지지면 없음"));
	}
}

void ALSShellSupplySchedule::AddDelivery(ALSShellTransport* Vehicle, int32 RouteIndex, int32 Ordinal)
{
	FLSActiveDelivery Delivery;
	Delivery.RouteIndex = RouteIndex; Delivery.Ordinal = Ordinal; Delivery.Vehicle = Vehicle;
	Delivery.UnloadLocation = Vehicle->RouteEnd;
	const TArray<FVector> ExitWaypoints = BuildExitWaypoints(Vehicle, RouteIndex, Delivery.UnloadLocation);
	if (!ExitWaypoints.IsEmpty()) Vehicle->SetDeliveryExitDirection(ExitWaypoints[0] - Delivery.UnloadLocation);
	for (ALSShellPhysicalProp* Cargo : Vehicle->GetCargoActors()) Delivery.Resources.Add(Cargo);
	Deliveries.Add(MoveTemp(Delivery));
	++DepartedCount;
	++SuccessfulDepartures[RouteIndex];
}

void ALSShellSupplySchedule::SetNotice(int32 RouteIndex, const FString& Text, bool bTerminalReplenishmentFailure)
{
	if (!bTerminalReplenishmentFailure && bTerminalReplenishmentNotice[RouteIndex] && GetWorld()->GetTimeSeconds() < NoticeExpires[RouteIndex])
	{
		UE_LOG(LogTemp, Display, TEXT("Shell supply %s"), *Text);
		return;
	}
	if (Notices[RouteIndex] == Text && GetWorld()->GetTimeSeconds() < NoticeExpires[RouteIndex]) return;
	Notices[RouteIndex] = Text;
	NoticeExpires[RouteIndex] = GetWorld()->GetTimeSeconds() + NoticeSeconds;
	bTerminalReplenishmentNotice[RouteIndex] = bTerminalReplenishmentFailure;
	UE_LOG(LogTemp, Display, TEXT("Shell supply %s"), *Text);
}

bool ALSShellSupplySchedule::TryDeparture(int32 RouteIndex, int32 Ordinal)
{
	const FLSShellSupplyRoute& AuthoredRoute = RouteIndex == 0 ? RouteA : RouteB;
	const int32 SuccessfulOrdinal = SuccessfulDepartures[RouteIndex] + 1;
	const bool bCharged = RouteIndex == 1 && SuccessfulOrdinal == 2;
	FLSShellSupplyRoute Route = AuthoredRoute;
	const TArray<FLSShellCargoSlot>& Slots = bCharged ? Route.ChargedCargoSlots : Route.CargoSlots;
	FString Reason;
	if (GetActiveDeliveryCount(RouteIndex) >= MaximumActiveDeliveriesPerRoute) Reason = TEXT("잔존 물자 한도");
	else if (!FindAvailableDepartureRoute(RouteIndex, Route)) Reason = TEXT("하역 후보 세 곳의 자리 또는 경로 점유");
	if (!Reason.IsEmpty())
	{
		++SkippedCount;
		SetNotice(RouteIndex, FString::Printf(TEXT("%s 보급 건너뜀 · %s"), RouteIndex == 0 ? TEXT("A") : TEXT("B"), *Reason));
		return false;
	}
	const FTransform Transform((Route.End - Route.Start).Rotation(), Route.Start);
	ALSShellTransport* Vehicle = GetWorld()->SpawnActorDeferred<ALSShellTransport>(TransportClass, Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Vehicle) return false;
	Vehicle->RouteStart = Route.Start; Vehicle->RouteEnd = Route.End; Vehicle->CargoSlots = Slots;
	Vehicle->FinishSpawning(Transform);
	if (Vehicle->DriveState == ELSShellTransportDriveState::Invalid || !Vehicle->GetPhysicalBody()->IsSimulatingPhysics())
	{
		// 첫 물리 스텝 전 생성이 실패한 물자만 취소한다. 이미 배송한 몸체에는 만료를 걸지 않는다.
		for (ALSShellPhysicalProp* CargoActor : Vehicle->GetCargoActors()) CargoActor->Destroy();
		Vehicle->Destroy();
		++SkippedCount;
		SetNotice(RouteIndex, TEXT("보급 생성 취소 · 실제 지지면 검사 실패"));
		return false;
	}
	AddDelivery(Vehicle, RouteIndex, SuccessfulOrdinal);
	UE_LOG(LogTemp, Display, TEXT("SHELL_SUPPLY_DEPARTURE Route=%d Scheduled=%d Successful=%d Charged=%d Unload=%s"),
		RouteIndex, Ordinal, SuccessfulOrdinal, bCharged, *Route.End.ToCompactString());
	SetNotice(RouteIndex, FString::Printf(TEXT("%s %s보급 출발"), RouteIndex == 0 ? TEXT("A") : TEXT("B"), bCharged ? TEXT("충전 화물 ") : TEXT("")));
	return true;
}

bool ALSShellSupplySchedule::TryReplenish(FLSActiveDelivery& Delivery)
{
	if (Delivery.bReplenishmentHandled) return false;
	if (Delivery.Ordinal % 2 != 0) { Delivery.bReplenishmentHandled = true; return true; }
	if (!Delivery.Vehicle.IsValid()) { ReportReplenishmentFailure(Delivery, TEXT("보급 차량 제거"), true); return false; }
	if (!Delivery.Vehicle->HasDeliveredCargo())
	{
		if (!ULSCombatTargetLibrary::IsCombatAlive(Delivery.Vehicle.Get())) ReportReplenishmentFailure(Delivery, TEXT("도착 전 보급 차량 무력화"), true);
		return false;
	}
	if (GetWorld()->GetTimeSeconds() < Delivery.NextReplenishmentAttemptSeconds) return false;
	Delivery.NextReplenishmentAttemptSeconds = GetWorld()->GetTimeSeconds() + ReplenishmentRetrySeconds;
	++Delivery.ReplenishmentAttempts;
	const FLSShellSupplyRoute& Route = Delivery.RouteIndex == 0 ? RouteA : RouteB;
	const bool bHeavy = Delivery.RouteIndex == 1;
	const float HalfHeight = bHeavy ? 140.f : 88.f;
	const float Radius = bHeavy ? 140.f : 42.f;
	FVector Position;
	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	const auto* Battlefield = ALSBattlefieldSettings::Find(GetWorld());
	bool bFoundPosition = false;
	for (int32 Side = 0; Side < 4 && Battlefield; ++Side)
	{
		const FVector Offset = Route.ReplenishmentOffset.RotateAngleAxis(90.f * Side, FVector::UpVector);
		FVector Candidate = Delivery.UnloadLocation + Offset;
		FHitResult Ground;
		if (!GetWorld()->LineTraceSingleByObjectType(Ground, Candidate + FVector(0, 0, 1000), Candidate - FVector(0, 0, 1000), GroundObjects)) continue;
		Candidate.Z = Ground.ImpactPoint.Z + HalfHeight + Offset.Z;
		const FBox2D Bounds = Battlefield->GetWorldBounds();
		if (Candidate.X - Radius < Bounds.Min.X || Candidate.X + Radius > Bounds.Max.X ||
			Candidate.Y - Radius < Bounds.Min.Y || Candidate.Y + Radius > Bounds.Max.Y) continue;
		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(Overlaps, Candidate, FQuat::Identity, FCollisionObjectQueryParams::AllObjects,
			FCollisionShape::MakeCapsule(Radius - UE_KINDA_SMALL_NUMBER, HalfHeight - UE_KINDA_SMALL_NUMBER));
		bool bClear = true;
		for (const FOverlapResult& Overlap : Overlaps)
			if (Overlap.Component.IsValid() && Overlap.Component->GetCollisionResponseToChannel(LSCollisionChannels::Unit) == ECR_Block) bClear = false;
		if (bClear) { Position = Candidate; bFoundPosition = true; break; }
	}
	if (!bFoundPosition)
	{
		ReportReplenishmentFailure(Delivery, TEXT("네 후보의 지면·경계·실제 자리 검사 실패"));
		return false;
	}
	const FTransform Transform(Position);
	auto* Enemy = GetWorld()->SpawnActorDeferred<ALSShellExperimentEnemy>(EnemyClass, Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Enemy) { ReportReplenishmentFailure(Delivery, TEXT("병력 액터 생성 실패")); return false; }
	Enemy->EnemyKind = bHeavy ? ELSShellEnemyKind::Heavy : ELSShellEnemyKind::Assault;
	Enemy->StatsTable = EnemyStatsTable;
	if (auto* Siege = Enemy->FindComponentByClass<ULSShellSiegeController>())
	{
		Siege->DeploymentLocation = Route.SiegeDeploymentLocation;
		Siege->InitialLoadSeconds = FMath::Max(Siege->InitialLoadSeconds, GetRequiredWarningSeconds());
	}
	Enemy->FinishSpawning(Transform);
	bool bSiegeReady = true;
	if (bHeavy)
	{
		auto* Siege = Enemy->FindComponentByClass<ULSShellSiegeController>();
		const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
		if (Siege && Coordinator && Siege->Phase == ELSShellSiegePhase::Disabled)
			Siege->BeginSiege(Coordinator->PlayerCastle, Route.SiegeDeploymentLocation);
		bSiegeReady = Siege && Siege->Phase != ELSShellSiegePhase::Disabled && Siege->Phase != ELSShellSiegePhase::Invalid;
	}
	TArray<FOverlapResult> ActualOverlaps;
	const UCapsuleComponent* ActualCapsule = Enemy->GetCapsuleComponent();
	GetWorld()->OverlapMultiByObjectType(ActualOverlaps, ActualCapsule->GetComponentLocation(), ActualCapsule->GetComponentQuat(),
		FCollisionObjectQueryParams::AllObjects, ActualCapsule->GetCollisionShape(-UE_KINDA_SMALL_NUMBER),
		FCollisionQueryParams(SCENE_QUERY_STAT(ShellSupplyActualTroopClearance), false, Enemy));
	bool bActualPoseClear = true;
	for (const FOverlapResult& Overlap : ActualOverlaps)
		if (Overlap.Component.IsValid() && ActualCapsule->GetCollisionResponseToChannel(Overlap.Component->GetCollisionObjectType()) == ECR_Block &&
			Overlap.Component->GetCollisionResponseToChannel(ActualCapsule->GetCollisionObjectType()) == ECR_Block) bActualPoseClear = false;
	if (!ULSCombatTargetLibrary::IsCombatAlive(Enemy) || !bSiegeReady || !bActualPoseClear)
	{
		Enemy->EndBattleActions();
		Enemy->Destroy();
		ReportReplenishmentFailure(Delivery, TEXT("공통 전투·공성 준비 또는 실제 몸체 검사 실패"));
		return false;
	}
	Delivery.Resources.Add(Enemy);
	Delivery.bReplenishmentHandled = true;
	++ReplenishedCount;
	SetNotice(Delivery.RouteIndex, bHeavy ? TEXT("B 보급 · 공성 병력 하역") : TEXT("A 보급 · 돌격 병력 하역"));
	return true;
}

void ALSShellSupplySchedule::ReportReplenishmentFailure(FLSActiveDelivery& Delivery, const FString& Reason, bool bTerminal)
{
	if (Delivery.bReplenishmentHandled) return;
	if (bTerminal || Delivery.ReplenishmentAttempts >= MaximumReplenishmentAttempts)
	{
		Delivery.bReplenishmentHandled = true;
		++FailedReplenishmentCount;
		SetNotice(Delivery.RouteIndex, FString::Printf(TEXT("병력 하역 실패 · %s · %d회 검사 후 종료"), *Reason, Delivery.ReplenishmentAttempts), true);
	}
	else if (Delivery.LastReplenishmentFailure != Reason)
		SetNotice(Delivery.RouteIndex, TEXT("병력 하역 대기 · ") + Reason);
	Delivery.LastReplenishmentFailure = Reason;
}

bool ALSShellSupplySchedule::TryArenaPressure(int32 EntryIndex)
{
	const ALSShellExperimentEnemy* Defaults = EnemyClass.GetDefaultObject();
	const UCapsuleComponent* Capsule = Defaults ? Defaults->GetCapsuleComponent() : nullptr;
	const auto* Battlefield = ALSBattlefieldSettings::Find(GetWorld());
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Capsule || !Battlefield || !Nav) return false;
	const float Radius = Capsule->GetScaledCapsuleRadius();
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	// CDO는 BeginPlay의 UpdateNavAgent 전이다. 실제 생성 캡슐과 같은 유효 크기로 질의한다.
	FNavAgentProperties Agent = Defaults->GetNavAgentPropertiesRef();
	Agent.AgentRadius = Radius;
	Agent.AgentHeight = HalfHeight * 2;
	const ANavigationData* Data = Nav->GetNavDataForProps(Agent, ArenaPressureEntries[EntryIndex]);
	if (!Data) return false;
	FCollisionObjectQueryParams GroundObjects;
	GroundObjects.AddObjectTypesToQuery(LSCollisionChannels::Ground);
	for (int32 Alternative = 0; Alternative < 2; ++Alternative)
	{
		FVector Candidate = ArenaPressureEntries[EntryIndex];
		Candidate.Y += Alternative * FMath::Sign(Candidate.Y) * ArenaPressureAlternateOffset;
		FHitResult Ground;
		if (!GetWorld()->LineTraceSingleByObjectType(Ground, Candidate + FVector(0, 0, 100), Candidate - FVector(0, 0, HalfHeight + 100), GroundObjects) ||
			Ground.ImpactNormal.Z < .99f) continue;
		Candidate.Z = Ground.ImpactPoint.Z + HalfHeight;
		const FBox2D Bounds = Battlefield->GetWorldBounds();
		if (Candidate.X - Radius < Bounds.Min.X || Candidate.X + Radius > Bounds.Max.X ||
			Candidate.Y - Radius < Bounds.Min.Y || Candidate.Y + Radius > Bounds.Max.Y) continue;
		FNavLocation Projected;
		if (!Nav->ProjectPointToNavigation(Ground.ImpactPoint, Projected, FVector(50, 50, 100), Data) ||
			FVector::DistXY(Projected.Location, Candidate) > 50 || FMath::Abs(Projected.Location.Z - Ground.ImpactPoint.Z) > 10) continue;
		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(Overlaps, Candidate, FQuat::Identity, FCollisionObjectQueryParams::AllObjects,
			Capsule->GetCollisionShape(-UE_KINDA_SMALL_NUMBER), FCollisionQueryParams(SCENE_QUERY_STAT(ShellPressureClearance), false, this));
		bool bClear = true;
		for (const FOverlapResult& Overlap : Overlaps)
			if (const UPrimitiveComponent* Other = Overlap.Component.Get(); Other &&
				Other->GetCollisionResponseToChannel(Capsule->GetCollisionObjectType()) == ECR_Block &&
				Capsule->GetCollisionResponseToChannel(Other->GetCollisionObjectType()) == ECR_Block) bClear = false;
		if (!bClear) continue;
		const FTransform Transform(Candidate);
		auto* Enemy = GetWorld()->SpawnActorDeferred<ALSShellExperimentEnemy>(EnemyClass, Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Enemy) continue;
		Enemy->EnemyKind = ELSShellEnemyKind::Assault;
		Enemy->StatsTable = EnemyStatsTable;
		Enemy->StatsRowName = NAME_None;
		Enemy->FinishSpawning(Transform);
		const UCapsuleComponent* ActualCapsule = Enemy->GetCapsuleComponent();
		Overlaps.Reset();
		GetWorld()->OverlapMultiByObjectType(Overlaps, ActualCapsule->GetComponentLocation(), ActualCapsule->GetComponentQuat(),
			FCollisionObjectQueryParams::AllObjects, ActualCapsule->GetCollisionShape(-UE_KINDA_SMALL_NUMBER),
			FCollisionQueryParams(SCENE_QUERY_STAT(ShellPressureActualClearance), false, Enemy));
		for (const FOverlapResult& Overlap : Overlaps)
			if (const UPrimitiveComponent* Other = Overlap.Component.Get(); Other &&
				Other->GetCollisionResponseToChannel(ActualCapsule->GetCollisionObjectType()) == ECR_Block &&
				ActualCapsule->GetCollisionResponseToChannel(Other->GetCollisionObjectType()) == ECR_Block) bClear = false;
		if (bClear && ULSCombatTargetLibrary::IsCombatAlive(Enemy)) return true;
		Enemy->EndBattleActions();
		Enemy->Destroy();
	}
	return false;
}

FString ALSShellSupplySchedule::GetArenaPressureStatus(double Elapsed) const
{
	if (!bArenaPressureEnabled || NextPressureTime >= ArenaPressureTimes.Num()) return FString();
	const double Remaining = ArenaPressureTimes[NextPressureTime] - Elapsed;
	if (Remaining > WarningSeconds) return FString();
	return FString::Printf(TEXT("측면 증원 %.0f초"), FMath::Max(0.0, Remaining));
}

void ALSShellSupplySchedule::UpdateArenaPressure(double Elapsed)
{
	while (NextPressureTime < ArenaPressureTimes.Num() && Elapsed >= ArenaPressureTimes[NextPressureTime])
	{
		for (int32 Index = 0; Index < ArenaPressureEntries.Num(); ++Index)
		{
			FLSPressureAttempt& Attempt = PressureAttempts.AddDefaulted_GetRef();
			Attempt.EntryIndex = Index;
			Attempt.NextAttempt = Elapsed;
		}
		++NextPressureTime;
	}
	for (int32 Index = PressureAttempts.Num() - 1; Index >= 0; --Index)
	{
		FLSPressureAttempt& Attempt = PressureAttempts[Index];
		if (Elapsed < Attempt.NextAttempt) continue;
		int32 AliveAssaults = 0;
		for (TActorIterator<ALSShellExperimentEnemy> It(GetWorld()); It; ++It)
			if (It->EnemyKind == ELSShellEnemyKind::Assault && ULSCombatTargetLibrary::IsCombatAlive(*It)) ++AliveAssaults;
		if (AliveAssaults >= ArenaPressureEnemyCap || TryArenaPressure(Attempt.EntryIndex))
		{
			PressureAttempts.RemoveAt(Index);
			continue;
		}
		++Attempt.Attempts;
		if (Attempt.Attempts >= ArenaPressureMaximumAttempts) PressureAttempts.RemoveAt(Index);
		else Attempt.NextAttempt = Elapsed + ArenaPressureRetrySeconds;
	}
	const FString Status = GetArenaPressureStatus(Elapsed);
	for (UStaticMeshComponent* Mark : PressureMarks) Mark->SetVisibility(true);
	for (UTextRenderComponent* Label : PressureLabels)
	{
		Label->SetVisibility(!Status.IsEmpty());
		Label->SetText(FText::FromString(Status));
	}
}

void ALSShellSupplySchedule::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!ConfigurationError.IsEmpty()) { UpdatePresentation(); return; }
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsBattleActive())
	{
		for (UTextRenderComponent* Label : RouteLabels) Label->SetVisibility(false);
		for (UTextRenderComponent* Label : PressureLabels) Label->SetVisibility(false);
		for (UStaticMeshComponent* Mark : PressureMarks) Mark->SetVisibility(false);
		PressureAttempts.Empty();
		return;
	}
	if (BattleStartSeconds < 0) BattleStartSeconds = GetWorld()->GetTimeSeconds();
	const double Elapsed = GetWorld()->GetTimeSeconds() - BattleStartSeconds;
	if (!bInitialAdopted)
	{
		if (!IsValid(InitialRouteATransport) || InitialRouteATransport->DriveState == ELSShellTransportDriveState::Invalid)
		{
			ConfigurationError = TEXT("초기 A 차량의 실제 적재가 실패했다.");
			return;
		}
		AddDelivery(InitialRouteATransport, 0, 1);
		bInitialAdopted = true;
		NextDepartures[0] = RouteA.FirstDepartureSeconds + RouteA.RepeatSeconds;
		if (auto* Siege = InitialHeavy->FindComponentByClass<ULSShellSiegeController>(); Siege && Siege->Phase == ELSShellSiegePhase::Disabled)
		{
			if (!Siege->BeginSiege(Coordinator->PlayerCastle, Siege->DeploymentLocation))
			{
				InitialHeavy->EndBattleActions();
				ConfigurationError = Siege->ConfigurationError;
				return;
			}
		}
		SetNotice(0, TEXT("A 보급 출발"));
	}
	Deliveries.RemoveAll([](const FLSActiveDelivery& Delivery) { return !HasRemainingResources(Delivery); });
	for (FLSActiveDelivery& Delivery : Deliveries)
	{
		if (!Delivery.bReplenishmentHandled) TryReplenish(Delivery);
		TryExit(Delivery);
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FLSShellSupplyRoute& Route = Index == 0 ? RouteA : RouteB;
		if (Elapsed < NextDepartures[Index]) continue;
		const int32 Ordinal = GetScheduledDepartureOrdinal(Route, Elapsed);
		TryDeparture(Index, Ordinal);
		// 낮은 프레임률이나 일시 정지 뒤에 지난 회차를 몰아서 생성하지 않는다.
		NextDepartures[Index] = Route.FirstDepartureSeconds + Ordinal * static_cast<double>(Route.RepeatSeconds);
	}
	if (bArenaPressureEnabled) UpdateArenaPressure(Elapsed);
	UpdatePresentation();
}

FString ALSShellSupplySchedule::GetStatusText() const
{
	if (!ConfigurationError.IsEmpty()) return ConfigurationError;
	if (!GetWorld() || BattleStartSeconds < 0) return TEXT("보급 준비");
	const auto* Coordinator = ALSShellExperimentCoordinator::Find(GetWorld());
	if (!Coordinator || !Coordinator->IsBattleActive()) return FString();
	const FString Pressure = GetArenaPressureStatus(GetWorld()->GetTimeSeconds() - BattleStartSeconds);
	if (!Pressure.IsEmpty())
	{
		const double Remaining = ArenaPressureTimes[NextPressureTime] - (GetWorld()->GetTimeSeconds() - BattleStartSeconds);
		bool bSoonest = true;
		for (int32 Index = 0; Index < 2; ++Index)
			if (GetActiveDeliveryCount(Index) < MaximumActiveDeliveriesPerRoute && NextDepartures[Index] - (GetWorld()->GetTimeSeconds() - BattleStartSeconds) < Remaining)
				bSoonest = false;
		if (bSoonest) return Pressure;
	}
	FString Text;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FString Line = GetRouteStatusText(Index);
		if (!Line.IsEmpty()) { if (!Text.IsEmpty()) Text += TEXT(" · "); Text += Line; }
	}
	return Text;
}

FString ALSShellSupplySchedule::GetRouteStatusText(int32 RouteIndex) const
{
	const double Elapsed = GetWorld()->GetTimeSeconds() - BattleStartSeconds;
	if (GetActiveDeliveryCount(RouteIndex) < MaximumActiveDeliveriesPerRoute && NextDepartures[RouteIndex] - Elapsed <= WarningSeconds)
	{
		FLSShellSupplyRoute AvailableRoute;
		FString Warning = FindAvailableDepartureRoute(RouteIndex, AvailableRoute)
			? FString::Printf(TEXT("%s %s보급 예고 %.0f초"), RouteIndex == 0 ? TEXT("A") : TEXT("B"),
				RouteIndex == 1 && SuccessfulDepartures[1] == 1 ? TEXT("충전 ") : TEXT(""), FMath::Max(0.0, NextDepartures[RouteIndex] - Elapsed))
			: FString::Printf(TEXT("%s 경로 점유 · 보급 대기"), RouteIndex == 0 ? TEXT("A") : TEXT("B"));
		if (bTerminalReplenishmentNotice[RouteIndex] && GetWorld()->GetTimeSeconds() < NoticeExpires[RouteIndex]) Warning += TEXT(" · 병력 하역 실패");
		return Warning;
	}
	return GetWorld()->GetTimeSeconds() < NoticeExpires[RouteIndex] ? Notices[RouteIndex] : FString();
}

void ALSShellSupplySchedule::UpdatePresentation()
{
	for (int32 Index = 0; Index < RouteLabels.Num(); ++Index)
	{
		UTextRenderComponent* Label = RouteLabels[Index];
		const FLSShellSupplyRoute& Route = Index == 0 ? RouteA : RouteB;
		Label->SetWorldLocation(Route.Start + FVector(0, 0, 450));
		const FString Text = !ConfigurationError.IsEmpty() ? ConfigurationError : GetRouteStatusText(Index);
		Label->SetVisibility(!Text.IsEmpty());
		Label->SetText(FText::FromString(Text));
	}
}
