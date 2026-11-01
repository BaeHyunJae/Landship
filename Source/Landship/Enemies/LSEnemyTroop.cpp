#include "Enemies/LSEnemyTroop.h"
#include "Enemies/LSEnemyBehaviorComponent.h"
#include "Controls/LSOrderComponent.h"

ALSEnemyTroop::ALSEnemyTroop()
{
	SetInitialFaction(ELSFaction::Enemy);
	Orders->PrimaryComponentTick.bStartWithTickEnabled = false;
	Behavior = CreateDefaultSubobject<ULSEnemyBehaviorComponent>(TEXT("EnemyBehavior"));
}
