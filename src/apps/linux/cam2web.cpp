/*
    cam2web - streaming camera to web

    Copyright (C) 2017, cvsandbox, cvsandbox@gmail.com

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
*/

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pwd.h>
#include <linux/limits.h>
#include <map>
#include <stdlib.h>
#include <iostream>


#include "XV4LCamera.hpp"
#include "XV4LCameraConfig.hpp"
#include "XWebServer.hpp"
#include "XVideoSourceToWeb.hpp"
#include "XObjectConfigurationSerializer.hpp"
#include "XObjectConfigurationRequestHandler.hpp"
#include "XManualResetEvent.hpp"

// Release build embeds web resources into executable
#ifdef NDEBUG
    #include "index.html.h"
    #include "styles.css.h"
    #include "cam2web.png.h"
    #include "cam2web_white.png.h"
    #include "camera.js.h"
    #include "cameraproperties.html.h"
    #include "cameraproperties.js.h"
    #include "jquery.js.h"
    #include "jquery.mobile.js.h"
    #include "jquery.mobile.css.h"
#endif

using namespace std;

// Information provided on version request
#define STR_INFO_PRODUCT        "cam2web"
#define STR_INFO_VERSION        "1.1.0"
#define STR_INFO_PLATFORM       "Linux"

// Name of the device and default title of the camera
const char* DEVICE_NAME = "Video for Linux Camera";

XManualResetEvent ExitEvent;

// Different application settings
struct
{
    uint32_t DeviceNumber;
    uint32_t FrameWidth;
    uint32_t FrameHeight;
    uint32_t FrameRate;
    uint32_t WebPort;
    string   HtRealm;
    string   HtDigestFileName;
    string   CameraConfigFileName;
    string   CustomWebContent;
    string   CameraTitle;
    UserGroup ViewersGroup;
    UserGroup ConfigGroup;
}
Settings;

// Raise exit event when signal is received
void sigIntHandler( int s )
{
    ExitEvent.Signal( );
}

// Listener for camera errors
class CameraErrorListener : public IVideoSourceListener
{
public:
    // New video frame notification - ignore it
    virtual void OnNewImage( const std::shared_ptr<const XImage>& image ) { };

    // Video source error notification
    virtual void OnError( const std::string& errorMessage, bool fatal )
    {
        printf( "[%s] : %s \n", ( ( fatal ) ? "Fatal" : "Error" ), errorMessage.c_str( ) );
        if ( fatal )
        {
            // time to exit if something has bad happened
            ExitEvent.Signal( );
        }
    }
};

// Set default values for settings
void SetDefaultSettings( )
{
    Settings.DeviceNumber = 0;
    Settings.FrameWidth   = 640;
    Settings.FrameHeight  = 480;
    Settings.FrameRate    = 20;
    Settings.WebPort      = 8000;

    Settings.HtRealm = "cam2web";
    Settings.HtDigestFileName.clear( );

    Settings.ViewersGroup = UserGroup::Anyone;
    Settings.ConfigGroup  = UserGroup::Anyone;

    struct passwd* pwd = getpwuid( getuid( ) );
    if ( pwd )
    {
        Settings.CameraConfigFileName  = pwd->pw_dir;
        Settings.CameraConfigFileName += "/.cam_config";
    }

#ifdef NDEBUG
    Settings.CustomWebContent.clear( );
#else
    // default location of web content for debug builds
    Settings.CustomWebContent = "./web";
#endif

    Settings.CameraTitle = DEVICE_NAME;
}


int main( int argc, char* argv[] )
{
    struct sigaction sigIntAction;

    SetDefaultSettings( );
    
    // set-up handler for certain signals
    sigIntAction.sa_handler = sigIntHandler;
    sigemptyset( &sigIntAction.sa_mask );
    sigIntAction.sa_flags = 0;

    sigaction( SIGINT,  &sigIntAction, NULL );
    sigaction( SIGQUIT, &sigIntAction, NULL );
    sigaction( SIGTERM, &sigIntAction, NULL );
    sigaction( SIGABRT, &sigIntAction, NULL );
    sigaction( SIGTERM, &sigIntAction, NULL );
    
    // create camera object
    shared_ptr<XV4LCamera>           xcamera       = XV4LCamera::Create( );
    shared_ptr<IObjectConfigurator>  xcameraConfig = make_shared<XV4LCameraConfig>( xcamera );
    XObjectConfigurationSerializer   serializer( Settings.CameraConfigFileName, xcameraConfig );

    // some read-only information about the version
    PropertyMap versionInfo;

    versionInfo.insert( PropertyMap::value_type( "product", STR_INFO_PRODUCT ) );
    versionInfo.insert( PropertyMap::value_type( "version", STR_INFO_VERSION ) );
    versionInfo.insert( PropertyMap::value_type( "platform", STR_INFO_PLATFORM ) );


    // prepare some read-only informational properties of the camera
    PropertyMap cameraInfo;
    char        strVideoSize[32];

    sprintf( strVideoSize,      "%u", Settings.FrameWidth );
    sprintf( strVideoSize + 16, "%u", Settings.FrameHeight );

    cameraInfo.insert( PropertyMap::value_type( "device", DEVICE_NAME ) );
    cameraInfo.insert( PropertyMap::value_type( "title",  Settings.CameraTitle ) );
    cameraInfo.insert( PropertyMap::value_type( "width",  strVideoSize ) );
    cameraInfo.insert( PropertyMap::value_type( "height", strVideoSize + 16 ) );

    // create and configure web server
    XWebServer          server( "", Settings.WebPort );
    XVideoSourceToWeb   video2web;
    UserGroup           viewersGroup = Settings.ViewersGroup;
    UserGroup           configGroup  = Settings.ConfigGroup;

    if ( !Settings.HtRealm.empty( ) )
    {
        server.SetAuthDomain( Settings.HtRealm );
    }
    if ( !Settings.HtDigestFileName.empty( ) )
    {
        server.LoadUsersFromFile( Settings.HtDigestFileName );
    }

    // set camera configuration
    xcamera->SetVideoDevice( Settings.DeviceNumber );
    xcamera->SetVideoSize( Settings.FrameWidth, Settings.FrameHeight );
    xcamera->SetFrameRate(Settings.FrameRate );

    // restore camera settings
    serializer.LoadConfiguration( );

    // set camera listeners
    XVideoSourceListenerChain   listenerChain;
    CameraErrorListener         cameraErrorListener;

    listenerChain.Add( video2web.VideoSourceListener( ) );
    listenerChain.Add( &cameraErrorListener );
    xcamera->SetListener( &listenerChain );
    

    printf("Camera Started \n");
        xcamera->Start( );

        while ( !ExitEvent.Wait( 60000 ) )
        {
            // save camera settings from time to time
            serializer.SaveConfiguration( );
        }

        serializer.SaveConfiguration( );

        xcamera->SignalToStop( );
        xcamera->WaitForStop( );
    
    return 0;
}

