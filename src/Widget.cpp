/* Widget.cpp: controller class
 *
 * Author:
 *	Andreia Gaita  <avidigal@novell.com>
 *
 * Copyright 2007 Novell, Inc. (http://www.novell.com)
 *
 * See the LICENSE file included with the distribution for details.
 * 
 */

#include "Widget.h"
#include "LocationProvider.h"

#ifdef NS_UNIX
#include <gtk/gtk.h>
#include "gtkWidget.h"

static gboolean gtk_invoke (gpointer data);

// from gluezilla.cpp
extern GThread    *ui_thread_id;
extern GAsyncQueue *queueout;
#endif	


// nsIProxyObjectManager includes nsProxyEvent.h, which brings
// all kinds of ugly dependencies with it, completely unnecessarily
// since the only thing that is really needed in that header are these
// three definitions. Hence, the idl that is included in the build/idl_extras
// has the include commented out. If we are building against that idl/header, 
// we use these definitions instead.
#ifndef PROXY_SYNC
#define PROXY_SYNC    0x0001  // acts just like a function call.
#define PROXY_ASYNC   0x0002  // fire and forget.  This will return immediately and you will lose all return information.
#define PROXY_ALWAYS  0x0004   // ignore check to see if the eventQ is on the same thread as the caller, and alway return a proxied object.
#endif

PRUint32 Widget::widgetCount = 0;

static NS_DEFINE_CID(kAppShellCID, NS_APPSHELL_CID);


nsresult 
Widget::BeginInvoke (Params * params)
{
	#ifdef NS_UNIX
	GThread *thread = g_thread_self ();
	if (thread != ui_thread_id) {
		g_idle_add (gtk_invoke, params);
		g_async_queue_pop (queueout);
	} else {
		return EndInvoke (params);
	}
	return NS_OK;
	#else
	return EndInvoke (params);
	#endif
			
}


#ifdef NS_UNIX
static gboolean gtk_invoke (gpointer data)
{
	Params * p = (Params *)data;
	Widget *widget = (Widget *)p->instance;

	gdk_threads_enter ();
	nsresult ret = widget->EndInvoke (p);
	gdk_threads_leave ();

	g_async_queue_push (queueout, &ret);
}
#endif

nsresult
Widget::GRE_Startup()
{
	const char* xpcomLocation = GetAvailableRuntime ();

	if (!xpcomLocation)
	{
        SHOUT("No Available runtime!");
        return -1;
	}

    // Startup the XPCOM Glue that links us up with XPCOM.
    nsresult rv = XPCOMGlueStartup(xpcomLocation);
    
    if (NS_FAILED(rv)) {
        SHOUT("gre: XPCOMGlueStartup failed");
        return rv;
    }

    LocationProvider *provider = new LocationProvider (this);
    if ( !provider ) {
        SHOUT("GRE_Startup failed");
        XPCOMGlueShutdown();
        return NS_ERROR_OUT_OF_MEMORY;
    }
	NS_ADDREF( provider );

    nsCOMPtr<nsIServiceManager> servMan;
    rv = NS_InitXPCOM2(getter_AddRefs(servMan), nsnull, provider);
    NS_RELEASE(provider);

    if ( NS_FAILED(rv) || !servMan) {
        SHOUT("gre: NS_InitXPCOM failed");
        XPCOMGlueShutdown();
        return rv;
    }

    return NS_OK;
}


nsresult
Widget::Load (CallbackBin *events)
{
	widgetCount++;

	if (widgetCount == 1)
	{
		nsresult rv;
		rv = this->GRE_Startup ();
		if (!NS_SUCCEEDED(rv)) {
			SHOUT("Failed to startup!\n");
			return -1;
		}

		nsCOMPtr<nsILocalFile> gre;
		rv = GetAvailableRuntime (getter_AddRefs(gre));
		if (!NS_SUCCEEDED(rv)) {
			SHOUT("Failed to find a runtime to run on!\n");
			return -1;
		}

		//nsDynamicFunctionLoad xreFunctions[] = {
		//  {"XRE_InitEmbedding", (NSFuncPtr*) &XRE_InitEmbedding},
		//  {"XRE_TermEmbedding", (NSFuncPtr*) &XRE_TermEmbedding},
		//  //{"GRE_GetCurrentProcessDirectory", (NSFuncPtr*) &GRE_GetCurrentProcessDirectory},
		//  {nsnull, nsnull}
		//}; 

		//rv = XPCOMGlueLoadXULFunctions(xreFunctions);
		//NS_ENSURE_SUCCESS(rv, rv);


		//nsCOMPtr<nsILocalFile> app;
		//nsEmbedCString file(startDir);
		//rv = NS_NewNativeLocalFile(file, PR_TRUE, getter_AddRefs(app));

		//XRE_InitEmbedding(gre, app, nsnull, nsnull, 0); 

		nsCOMPtr<nsIAppShell> appShell;
		appShell = do_CreateInstance(kAppShellCID);
		if (!appShell) {
		  SHOUT("Failed to create appshell in Widget::Load!\n");
		  return -1;
		}
		this->appShell = appShell.get();
		NS_ADDREF(this->appShell);
		this->appShell->Create(0, nsnull);
		this->appShell->Spinup();

	}
	this->events = events;
	events->OnWidgetLoaded ();
	return 0;
}

void
Widget::Shutdown ()
{
	widgetCount--;

	if (widgetCount == 0) {
		if (appShell) {
			// Shutdown the appshell service.
			this->appShell->Spindown();
			NS_RELEASE(this->appShell);
			this->appShell = 0;
		}
		GRE_Shutdown();
		#ifdef NS_UNIX
		g_idle_add (gtk_shutdown, NULL);
		#endif
	}
}

nsresult 
Widget::Init(Handle *hwnd, PRUint32 width, PRUint32 height)
{
	this->width = width;
	this->height = height;
	this->hwnd = hwnd;

#ifdef NS_UNIX
	gdk_threads_enter ();
	GtkWidget *embed = native_embed_widget_foreign_new((GdkNativeWindow)(GPOINTER_TO_INT(hwnd)));
	gtk_widget_set_usize(embed, width, height);
	gtk_widget_show(embed);
	gdk_threads_leave ();
	this->hwnd = (Handle*)embed;
#endif

	nsCOMPtr< nsIWebBrowser > newBrowser;

	browserWindow = new BrowserWindow ();
	if ( ! browserWindow )
		return -1;
 
	browserWindow->AddRef ();
	browserWindow->SetChromeFlags( nsIWebBrowserChrome::CHROME_ALL );

	this->CreateBrowserWindow ();
	return NS_OK;	
}

nsresult 
Widget::CreateBrowserWindow()
{
	PRINT("Widget::CreateBrowserWindow!\n");
	browserWindow->setParent( this );
	nsresult ret = browserWindow->Create( this->hwnd, this->width, this->height );
	Handle * nativeMozWindow = browserWindow->getNativeWin ();
	this->Navigate ("about:blank");
	return NS_OK;


	//return -1;
}


// Layout
nsresult 
Widget::Focus (FocusOption focus)
{
	PRINT("Widget::Focus!\n");
	this->Activate ();
	if (focus == FOCUS_NONE)
		this->browserWindow->Focus ();
	else {
		nsresult rv;
		nsCOMPtr<nsIWebBrowser> webBrowser;
		rv = browserWindow->GetWebBrowser(getter_AddRefs(webBrowser));
		if (NS_FAILED(rv))
			return NS_ERROR_FAILURE;

		nsCOMPtr<nsIWebBrowserFocus> webBrowserFocus(do_QueryInterface(webBrowser));
		if (!webBrowserFocus)
			return NS_ERROR_FAILURE;

		if (focus == FOCUS_FIRST)
			webBrowserFocus->SetFocusAtFirstElement();
		else if (FOCUS_LAST)
			webBrowserFocus->SetFocusAtLastElement();
	}
	
	return NS_OK;
}

nsresult
Widget::Blur ()
{
	PRINT("Widget::Blur!\n");
	this->Deactivate ();
	return NS_OK;
}

nsresult 
Widget::Activate ()
{
	PRINT("Widget::Activate!\n");
	nsresult rv;
	nsCOMPtr<nsIWebBrowser> webBrowser;
	rv = browserWindow->GetWebBrowser(getter_AddRefs(webBrowser));
	if (NS_FAILED(rv))
		return NS_ERROR_FAILURE;

	nsCOMPtr<nsIWebBrowserFocus> webBrowserFocus(do_QueryInterface(webBrowser));
	if (!webBrowserFocus)
		return NS_ERROR_FAILURE;

	webBrowserFocus->Activate();
	return NS_OK;
}

nsresult 
Widget::Deactivate ()
{
	PRINT("Widget::Deactivate!\n");
	nsresult rv;
	nsCOMPtr<nsIWebBrowser> webBrowser;
	rv = browserWindow->GetWebBrowser(getter_AddRefs(webBrowser));
	if (NS_FAILED(rv))
		return NS_ERROR_FAILURE;

	nsCOMPtr<nsIWebBrowserFocus> webBrowserFocus(do_QueryInterface(webBrowser));
	if (!webBrowserFocus)
		return NS_ERROR_FAILURE;

	webBrowserFocus->Deactivate();
	return NS_OK;
}


nsresult 
Widget::Resize (PRUint32 width, PRUint32 height)
{
	PRINT("Widget::Resize!\n");
	browserWindow->SetDimensions(nsIEmbeddingSiteWindow::DIM_FLAGS_POSITION |
								nsIEmbeddingSiteWindow::DIM_FLAGS_SIZE_INNER,
								0, 0, width, height);
#ifdef NS_UNIX
	gtk_widget_set_usize(reinterpret_cast<GtkWidget*>(this->hwnd), width, height);
#endif

	return NS_ERROR_FAILURE;
}


// Navigation
nsresult 
Widget::Navigate (const char * uri)
{
	PRINT("Widget::Navigate!\n");	
	return this->Navigate (NS_ConvertUTF8toUTF16(uri));
	return NS_ERROR_FAILURE;
}

nsresult 
Widget::Navigate (nsString uri)
{
	PRINT("Widget::Navigate!\n");
	if (browserWindow)
		return browserWindow->Navigate (uri);
	return NS_ERROR_FAILURE;
}

nsresult 
Widget::Forward ()
{
	PRINT("Widget::Forward!\n");
	if (browserWindow)
		return browserWindow->Forward ();
	return NS_ERROR_FAILURE;
}

nsresult 
Widget::Back ()
{
	PRINT("Widget::Back!\n");
	if (browserWindow)
		return browserWindow->Back ();
	return NS_ERROR_FAILURE;
}

nsresult 
Widget::Home ()
{
	PRINT("Widget::Home!\n");
	if (browserWindow)
		return browserWindow->Home ();
	return NS_ERROR_FAILURE;
}

nsresult 
Widget::Stop ()
{
	PRINT("Widget::Stop!\n");
	if (browserWindow)
		return browserWindow->Stop ();
	return NS_ERROR_FAILURE;
}

nsresult 
Widget::Reload (ReloadOption option)
{
	PRINT("Widget::Reload!\n");
	if (browserWindow)
		return browserWindow->Reload (option);
	return NS_ERROR_FAILURE;
}

// proxy getters

nsresult
Widget::GetProxyForObject (REFNSIID iid, nsISupports *object, void **result)
{
	nsresult rv;
	nsIProxyObjectManager * proxyManager = nsnull;
	rv = CallGetService (NS_XPCOMPROXY_CONTRACTID, &proxyManager);
	if (NS_FAILED (rv)) return rv;
	
	rv = proxyManager->GetProxyForObject (nsnull, 
										  iid, 
										  object, 
										  PROXY_SYNC | PROXY_ALWAYS, 
										  result);
	if (NS_FAILED (rv)) return rv;

	// GetProxyForObject addrefs the object yet again, so let it go
	// Note: The docs recommend releasing the object since the proxy refs it, 
	// but I'm getting wrong refcounts if I do, so for now it's off
	// NS_RELEASE (object);
	return rv;
}

nsresult
Widget::GetProxyForDocument ()
{
	nsCOMPtr<nsIDOMWindow> domWindow;		
	this->browserWindow->webBrowser->GetContentDOMWindow( getter_AddRefs (domWindow) );
	nsCOMPtr<nsIDOMDocument> domDoc;
	domWindow->GetDocument (getter_AddRefs(domDoc));
	nsCOMPtr<nsIDOMHTMLDocument> htmlDoc (do_QueryInterface( domDoc ));		
	return GetProxyForObject (nsIDOMHTMLDocument::GetIID(), htmlDoc, getter_AddRefs (this->document));
}

nsresult
Widget::GetProxyForNavigation ()
{
	nsCOMPtr<nsIWebNavigation> navigation (do_QueryInterface (this->browserWindow->webBrowser));
	return GetProxyForObject (nsIWebNavigation::GetIID(), navigation, getter_AddRefs (this->webNav));
}
// EVENTS

PRBool
Widget::EventDomKeyDown (KeyInfo keyInfo, ModifierKeys modifiers)
{
	return events->OnDomKeyDown (keyInfo, modifiers);
}

PRBool
Widget::EventDomKeyUp (KeyInfo keyInfo, ModifierKeys modifiers)
{
	return events->OnDomKeyUp (keyInfo, modifiers);
}

PRBool
Widget::EventDomKeyPress (KeyInfo keyInfo, ModifierKeys modifiers)
{
	return events->OnDomKeyPress (keyInfo, modifiers);
}

PRBool
Widget::EventMouseDown (MouseInfo mouseInfo, ModifierKeys modifiers)
{
	return events->OnMouseDown (mouseInfo, modifiers);
}

PRBool
Widget::EventMouseUp (MouseInfo mouseInfo, ModifierKeys modifiers)
{
	return events->OnMouseUp (mouseInfo, modifiers);
}

PRBool
Widget::EventMouseClick (MouseInfo mouseInfo, ModifierKeys modifiers)
{
	return events->OnMouseClick (mouseInfo, modifiers);
}

PRBool
Widget::EventMouseDoubleClick (MouseInfo mouseInfo, ModifierKeys modifiers)
{
	return events->OnMouseDoubleClick (mouseInfo, modifiers);
}

PRBool
Widget::EventMouseOver (MouseInfo mouseInfo, ModifierKeys modifiers)
{
	return events->OnMouseOver (mouseInfo, modifiers);

}

PRBool
Widget::EventMouseOut (MouseInfo mouseInfo, ModifierKeys modifiers)
{
	return events->OnMouseOut (mouseInfo, modifiers);
}

PRBool 
Widget::EventActivate ()
{
	return events->OnActivate ();
}

PRBool 
Widget::EventBeforeURIOpen (const char * uri)
{
	return events->OnBeforeURIOpen (uri);
}

PRBool 
Widget::EventCreateNewWindow ()
{
	return events->OnCreateNewWindow ();
}

void 
Widget::EventStateChange (PRUint32 state, PRInt32 status)
{
	events->OnStateChange (state, status);
}

void 
Widget::EventLocationChanged (const char * uri)
{
	events->OnLocationChanged (uri);
}


void
Widget::EventGeneric (nsString type)
{
	PRUnichar * t = (PRUnichar*)NS_StringCloneData(type);
	events->OnGeneric (t);
}
