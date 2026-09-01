/* Header file automatically generated from XamlMetaDataProvider.idl */
/*
 * File built with Microsoft(R) MIDLRT Compiler Engine Version 10.00.0231 
 */

#pragma warning( disable: 4049 )  /* more than 64k source lines */

/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 500
#endif

/* verify that the <rpcsal.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCSAL_H_VERSION__
#define __REQUIRED_RPCSAL_H_VERSION__ 100
#endif

#include <rpc.h>
#include <rpcndr.h>

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif /* __RPCNDR_H_VERSION__ */

#ifndef COM_NO_WINDOWS_H
#include <windows.h>
#include <ole2.h>
#endif /*COM_NO_WINDOWS_H*/
#ifndef __XamlMetaDataProvider_h_h__
#define __XamlMetaDataProvider_h_h__
#ifndef __XamlMetaDataProvider_h_p_h__
#define __XamlMetaDataProvider_h_p_h__


#pragma once

// Ensure that the setting of the /ns_prefix command line switch is consistent for all headers.
// If you get an error from the compiler indicating "warning C4005: 'CHECK_NS_PREFIX_STATE': macro redefinition", this
// indicates that you have included two different headers with different settings for the /ns_prefix MIDL command line switch
#if !defined(DISABLE_NS_PREFIX_CHECKS)
#define CHECK_NS_PREFIX_STATE "always"
#endif // !defined(DISABLE_NS_PREFIX_CHECKS)


#pragma push_macro("MIDL_CONST_ID")
#undef MIDL_CONST_ID
#define MIDL_CONST_ID const __declspec(selectany)


// Header files for imported files
#include "winrtbase.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.InteractiveExperiences.1.8.260708001\metadata\10.0.18362.0\Microsoft.Foundation.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.AI.1.8.79\metadata\Microsoft.Graphics.Imaging.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.InteractiveExperiences.1.8.260708001\metadata\10.0.18362.0\Microsoft.Graphics.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Security.Authentication.OAuth.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.WinUI.1.8.260803003\metadata\Microsoft.UI.Text.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.InteractiveExperiences.1.8.260708001\metadata\10.0.18362.0\Microsoft.UI.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.WinUI.1.8.260803003\metadata\Microsoft.UI.Xaml.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.Web.WebView2.1.0.3179.45\lib\Microsoft.Web.WebView2.Core.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.AI.1.8.79\metadata\Microsoft.Windows.AI.ContentSafety.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.AI.1.8.79\metadata\Microsoft.Windows.AI.Foundation.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.AI.1.8.79\metadata\Microsoft.Windows.AI.Imaging.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.ML.1.8.2197\metadata\Microsoft.Windows.AI.MachineLearning.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.AI.1.8.79\metadata\Microsoft.Windows.AI.Text.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.AI.1.8.79\metadata\Microsoft.Windows.AI.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.ApplicationModel.Background.UniversalBGTask.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.ApplicationModel.Background.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.ApplicationModel.DynamicDependency.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.ApplicationModel.Resources.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.ApplicationModel.WindowsAppRuntime.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.AppLifecycle.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.AppNotifications.Builder.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.AppNotifications.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.BadgeNotifications.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.Foundation.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.Globalization.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.Management.Deployment.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.Media.Capture.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.PushNotifications.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.Security.AccessControl.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.Storage.Pickers.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.Storage.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.System.Power.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Foundation.1.8.260803002\metadata\Microsoft.Windows.System.h"
#include "D:\MyProjects\Orbit\packages\Microsoft.WindowsAppSDK.Widgets.1.8.251231004\metadata\Microsoft.Windows.Widgets.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.AI.MachineLearning.MachineLearningContract\5.0.0.0\Windows.AI.MachineLearning.MachineLearningContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.AI.MachineLearning.Preview.MachineLearningPreviewContract\2.0.0.0\Windows.AI.MachineLearning.Preview.MachineLearningPreviewContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.ApplicationModel.Calls.Background.CallsBackgroundContract\4.0.0.0\Windows.ApplicationModel.Calls.Background.CallsBackgroundContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.ApplicationModel.Calls.CallsPhoneContract\7.0.0.0\Windows.ApplicationModel.Calls.CallsPhoneContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.ApplicationModel.Calls.CallsVoipContract\5.0.0.0\Windows.ApplicationModel.Calls.CallsVoipContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.ApplicationModel.CommunicationBlocking.CommunicationBlockingContract\2.0.0.0\Windows.ApplicationModel.CommunicationBlocking.CommunicationBlockingContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.ApplicationModel.SocialInfo.SocialInfoContract\2.0.0.0\Windows.ApplicationModel.SocialInfo.SocialInfoContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.ApplicationModel.StartupTaskContract\3.0.0.0\Windows.ApplicationModel.StartupTaskContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Devices.Custom.CustomDeviceContract\1.0.0.0\Windows.Devices.Custom.CustomDeviceContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Devices.DevicesLowLevelContract\3.0.0.0\Windows.Devices.DevicesLowLevelContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Devices.Printers.PrintersContract\1.0.0.0\Windows.Devices.Printers.PrintersContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Devices.SmartCards.SmartCardBackgroundTriggerContract\3.0.0.0\Windows.Devices.SmartCards.SmartCardBackgroundTriggerContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Devices.SmartCards.SmartCardEmulatorContract\6.0.0.0\Windows.Devices.SmartCards.SmartCardEmulatorContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Foundation.FoundationContract\4.0.0.0\Windows.Foundation.FoundationContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Foundation.UniversalApiContract\19.0.0.0\Windows.Foundation.UniversalApiContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Gaming.XboxLive.StorageApiContract\1.0.0.0\Windows.Gaming.XboxLive.StorageApiContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Graphics.Printing3D.Printing3DContract\4.0.0.0\Windows.Graphics.Printing3D.Printing3DContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Networking.Connectivity.WwanContract\3.0.0.0\Windows.Networking.Connectivity.WwanContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Networking.Sockets.ControlChannelTriggerContract\3.0.0.0\Windows.Networking.Sockets.ControlChannelTriggerContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Security.Isolation.IsolatedWindowsEnvironmentContract\5.0.0.0\Windows.Security.Isolation.Isolatedwindowsenvironmentcontract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Services.Maps.GuidanceContract\3.0.0.0\Windows.Services.Maps.GuidanceContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Services.Maps.LocalSearchContract\4.0.0.0\Windows.Services.Maps.LocalSearchContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Services.Store.StoreContract\4.0.0.0\Windows.Services.Store.StoreContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Services.TargetedContent.TargetedContentContract\1.0.0.0\Windows.Services.TargetedContent.TargetedContentContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.Storage.Provider.CloudFilesContract\7.0.0.0\Windows.Storage.Provider.CloudFilesContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.System.Profile.ProfileHardwareTokenContract\1.0.0.0\Windows.System.Profile.ProfileHardwareTokenContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.System.Profile.ProfileRetailInfoContract\1.0.0.0\Windows.System.Profile.ProfileRetailInfoContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.System.Profile.ProfileSharedModeContract\2.0.0.0\Windows.System.Profile.ProfileSharedModeContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.System.Profile.SystemManufacturers.SystemManufacturersContract\3.0.0.0\Windows.System.Profile.SystemManufacturers.SystemManufacturersContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.System.SystemManagementContract\7.0.0.0\Windows.System.SystemManagementContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.UI.UIAutomation.UIAutomationContract\2.0.0.0\Windows.UI.UIAutomation.UIAutomationContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.UI.ViewManagement.ViewManagementViewScalingContract\1.0.0.0\Windows.UI.ViewManagement.ViewManagementViewScalingContract.h"
#include "E:\Windows Kits\10\References\10.0.26100.0\Windows.UI.Xaml.Core.Direct.XamlDirectContract\5.0.0.0\Windows.UI.Xaml.Core.Direct.XamlDirectContract.h"

#if defined(__cplusplus) && !defined(CINTERFACE)
#if defined(__MIDL_USE_C_ENUM)
#define MIDL_ENUM enum
#else
#define MIDL_ENUM enum class
#endif
/* Forward Declarations */

namespace ABI {
    namespace Orbit {
        class XamlMetaDataProvider;
    } /* Orbit */
} /* ABI */

#ifndef ____x_ABI_CMicrosoft_CUI_CXaml_CMarkup_CIXamlMetadataProvider_FWD_DEFINED__
#define ____x_ABI_CMicrosoft_CUI_CXaml_CMarkup_CIXamlMetadataProvider_FWD_DEFINED__
namespace ABI {
    namespace Microsoft {
        namespace UI {
            namespace Xaml {
                namespace Markup {
                    interface IXamlMetadataProvider;
                } /* Markup */
            } /* Xaml */
        } /* UI */
    } /* Microsoft */
} /* ABI */
#define __x_ABI_CMicrosoft_CUI_CXaml_CMarkup_CIXamlMetadataProvider ABI::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider

#endif // ____x_ABI_CMicrosoft_CUI_CXaml_CMarkup_CIXamlMetadataProvider_FWD_DEFINED__



/*
 *
 * Class Orbit.XamlMetaDataProvider
 *
 * RuntimeClass can be activated.
 *
 * Class implements the following interfaces:
 *    Microsoft.UI.Xaml.Markup.IXamlMetadataProvider ** Default Interface **
 *
 * Class Threading Model:  Both Single and Multi Threaded Apartment
 *
 * Class Marshaling Behavior:  Agile - Class is agile
 *
 */

#ifndef RUNTIMECLASS_Orbit_XamlMetaDataProvider_DEFINED
#define RUNTIMECLASS_Orbit_XamlMetaDataProvider_DEFINED
extern const __declspec(selectany) _Null_terminated_ WCHAR RuntimeClass_Orbit_XamlMetaDataProvider[] = L"Orbit.XamlMetaDataProvider";
#endif


#else // !defined(__cplusplus)
/* Forward Declarations */
#ifndef ____x_ABI_CMicrosoft_CUI_CXaml_CMarkup_CIXamlMetadataProvider_FWD_DEFINED__
#define ____x_ABI_CMicrosoft_CUI_CXaml_CMarkup_CIXamlMetadataProvider_FWD_DEFINED__
typedef interface __x_ABI_CMicrosoft_CUI_CXaml_CMarkup_CIXamlMetadataProvider __x_ABI_CMicrosoft_CUI_CXaml_CMarkup_CIXamlMetadataProvider;

#endif // ____x_ABI_CMicrosoft_CUI_CXaml_CMarkup_CIXamlMetadataProvider_FWD_DEFINED__



/*
 *
 * Class Orbit.XamlMetaDataProvider
 *
 * RuntimeClass can be activated.
 *
 * Class implements the following interfaces:
 *    Microsoft.UI.Xaml.Markup.IXamlMetadataProvider ** Default Interface **
 *
 * Class Threading Model:  Both Single and Multi Threaded Apartment
 *
 * Class Marshaling Behavior:  Agile - Class is agile
 *
 */

#ifndef RUNTIMECLASS_Orbit_XamlMetaDataProvider_DEFINED
#define RUNTIMECLASS_Orbit_XamlMetaDataProvider_DEFINED
extern const __declspec(selectany) _Null_terminated_ WCHAR RuntimeClass_Orbit_XamlMetaDataProvider[] = L"Orbit.XamlMetaDataProvider";
#endif


#endif // defined(__cplusplus)
#pragma pop_macro("MIDL_CONST_ID")
#endif // __XamlMetaDataProvider_h_p_h__

#endif // __XamlMetaDataProvider_h_h__
