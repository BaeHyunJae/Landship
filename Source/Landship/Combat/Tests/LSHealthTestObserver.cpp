#include "Combat/Tests/LSHealthTestObserver.h"

void ULSHealthTestObserver::OnDamage(const FLSDamageResult& Damage)
{
	DamageResults.Add(Damage);
	if (DamageCallback)
	{
		DamageCallback(Damage);
	}
}

void ULSHealthTestObserver::OnDeath(const FLSDamageResult& Damage)
{
	DeathResults.Add(Damage);
	if (DeathCallback)
	{
		DeathCallback(Damage);
	}
}

void ULSHealthTestObserver::OnCollisionChanged(UPrimitiveComponent* ChangedComponent)
{
	if (CollisionCallback)
	{
		CollisionCallback(ChangedComponent);
	}
}
