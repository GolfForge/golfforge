#include "Drivers/LaunchMonitorDriver.h"

#include "Drivers/LaunchMonitorManager.h"
#include "Events/EventTypes.h"

void ULaunchMonitorDriver::SetManager(ULaunchMonitorManager* InManager)
{
	Manager = InManager;
}

void ULaunchMonitorDriver::PublishShot(const FShotTakenEvent& Shot)
{
	if (ULaunchMonitorManager* M = Manager.Get())
	{
		M->PublishShot(Shot);
	}
}

void ULaunchMonitorDriver::SetStatus(bool bInConnected, const FString& Detail)
{
	bConnected = bInConnected;
	if (OnStatusChanged)
	{
		OnStatusChanged(bInConnected, Detail);
	}
}

void ULaunchMonitorDriver::SetReady(bool bReady)
{
	if (OnReadyChanged)
	{
		OnReadyChanged(bReady);
	}
}
