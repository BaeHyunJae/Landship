#pragma once
#include "Vision/LSMinimapWidget.h"
#include "LSMinimapInputTestWidget.generated.h"

/** 네이티브 포인터 경로를 실제 좌표 변환까지 포함해 검사하는 위젯이다. */
UCLASS(Transient, NotBlueprintable)
class ULSMinimapInputTestWidget : public ULSMinimapWidget
{
	GENERATED_BODY()
  public:
	using ULSMinimapWidget::NativeOnMouseButtonDown;
};
