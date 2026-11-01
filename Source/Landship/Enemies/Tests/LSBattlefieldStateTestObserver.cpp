#include "Enemies/Tests/LSBattlefieldStateTestObserver.h"

void ULSBattlefieldStateTestObserver::OnCaptured(const FLSOutpostCaptureSnapshot& Capture)
{
	Captures.Add(Capture);
	if (CaptureCallback)
	{
		CaptureCallback(Capture);
	}
}
