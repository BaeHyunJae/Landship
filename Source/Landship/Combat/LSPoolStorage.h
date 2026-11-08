#pragma once

#include "CoreMinimal.h"

/** 소유권을 갖지 않는 활성 대여 기록. 객체와 발급 세대가 모두 일치해야 한 번 반환할 수 있다. */
class LANDSHIP_API FLSPoolLeases
{
  public:
	uint64 Acquire(UObject* Object)
	{
		if (!IsValid(Object) || Active.Contains(Object))
			return 0;
		if (++AcquisitionsSinceCleanup >= 256)
		{
			for (auto It = Active.CreateIterator(); It; ++It)
				if (!It.Key().IsValid())
					It.RemoveCurrent();
			AcquisitionsSinceCleanup = 0;
		}
		if (++NextSerial == 0)
			++NextSerial;
		Active.Add(Object, NextSerial);
		return NextSerial;
	}
	bool Contains(const UObject* Object, uint64 Serial) const
	{
		const uint64* Found = Active.Find(Object);
		return Serial != 0 && Found && *Found == Serial;
	}
	bool Release(UObject* Object, uint64 Serial)
	{
		if (!IsValid(Object) || !Contains(Object, Serial))
			return false;
		Active.Remove(Object);
		return true;
	}
	void Forget(UObject* Object)
	{
		Active.Remove(Object);
	}
	bool HasObject(const UObject* Object) const
	{
		return Active.Contains(Object);
	}
	void Reset()
	{
		Active.Reset();
		AcquisitionsSinceCleanup = 0;
	}

  private:
	TMap<TWeakObjectPtr<UObject>, uint64> Active;
	uint64 NextSerial = 0;
	int32 AcquisitionsSinceCleanup = 0;
};

namespace LSPoolStorage
{
template <typename T> T* PopValid(TArray<TObjectPtr<T>>& Inactive)
{
	while (!Inactive.IsEmpty())
	{
		T* Object = Inactive.Pop(EAllowShrinking::No);
		if (IsValid(Object) && !Object->IsActorBeingDestroyed())
			return Object;
	}
	return nullptr;
}
inline bool CanRetain(int32 InactiveCount, int32 Limit)
{
	return Limit == 0 || InactiveCount < Limit;
}
} // namespace LSPoolStorage
