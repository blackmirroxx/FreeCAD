/***************************************************************************
 *   Copyright (c) 2002 Jürgen Riegel <juergen.riegel@web.de>              *
 *                                                                         *
 *   This file is part of the FreeCAD CAx development system.              *
 *                                                                         *
 *   This library is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU Library General Public           *
 *   License as published by the Free Software Foundation; either          *
 *   version 2 of the License, or (at your option) any later version.      *
 *                                                                         *
 *   This library  is distributed in the hope that it will be useful,      *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU Library General Public License for more details.                  *
 *                                                                         *
 *   You should have received a copy of the GNU Library General Public     *
 *   License along with this library; see the file COPYING.LIB. If not,    *
 *   write to the Free Software Foundation, Inc., 59 Temple Place,         *
 *   Suite 330, Boston, MA  02111-1307, USA                                *
 *                                                                         *
 ***************************************************************************/

#include "PreCompiled.h"
#ifndef _PreComp_
# include <sstream>

# include <Precision.hxx>
#endif

#include <App/Application.h>
#include <App/Document.h>
#include <App/Link.h>
#include <Base/Console.h>
#include <Base/Parameter.h>

#include "DrawPage.h"
#include "DrawPagePy.h" // generated from DrawPagePy.xml
#include "DrawProjGroup.h"
#include "DrawTemplate.h"
#include "DrawView.h"
#include "DrawViewBalloon.h"
#include "DrawViewDimension.h"
#include "DrawViewPart.h"
#include "Preferences.h"
#include "DrawUtil.h"


using namespace TechDraw;

//===========================================================================
// DrawPage
//===========================================================================

App::PropertyFloatConstraint::Constraints DrawPage::scaleRange = {
    Precision::Confusion(), std::numeric_limits<double>::max(), (0.1)};// increment by 0.1

PROPERTY_SOURCE(TechDraw::DrawPage, App::DocumentObject)

const char* DrawPage::ProjectionTypeEnums[] = {"First Angle", "Third Angle", nullptr};

DrawPage::DrawPage(void)
{
    static const char* group = "Page";
    nowUnsetting = false;
    forceRedraw(false);

    ADD_PROPERTY_TYPE(KeepUpdated, (Preferences::keepPagesUpToDate()), group,
                      (App::PropertyType)(App::Prop_Output), "Keep page in sync with model");
    ADD_PROPERTY_TYPE(Template, (nullptr), group, (App::PropertyType)(App::Prop_None),
                      "Attached Template");
    Template.setScope(App::LinkScope::Global);
    ADD_PROPERTY_TYPE(Views, (nullptr), group, (App::PropertyType)(App::Prop_None),
                      "Attached Views");
    Views.setScope(App::LinkScope::Global);

    // Projection Properties
    ProjectionType.setEnums(ProjectionTypeEnums);
    ADD_PROPERTY(ProjectionType, ((long)Preferences::projectionAngle()));

    double defScale = Preferences::getPreferenceGroup("General")->GetFloat("DefaultScale", 1.0);
    ADD_PROPERTY_TYPE(Scale, (defScale), group, (App::PropertyType)(App::Prop_None),
                      "Scale factor for this Page");

    ADD_PROPERTY_TYPE(NextBalloonIndex, (1), group, (App::PropertyType)(App::Prop_None),
                      "Auto-numbering for Balloons");

    Scale.setConstraints(&scaleRange);
}

DrawPage::~DrawPage() {}

void DrawPage::onBeforeChange(const App::Property* prop)
{
    App::DocumentObject::onBeforeChange(prop);
}

void DrawPage::onChanged(const App::Property* prop)
{
    if (prop == &KeepUpdated && KeepUpdated.getValue()) {
        if (!isRestoring() && !isUnsetting()) {
            //would be nice if this message was displayed immediately instead of after the recomputeFeature
            Base::Console().message("Rebuilding Views for: %s/%s\n", getNameInDocument(),
                                    Label.getValue());
            updateAllViews();
            purgeTouched();
        }
    }
    else if (prop == &Template) {
        if (!isRestoring() && !isUnsetting()) {
            //nothing to page to do??
        }
    }
    else if (prop == &Scale) {
        // touch all views in the Page as they may be dependent on this scale
        // WF: not sure this loop is required.  Views figure out their scale as required. but maybe
        //     this is needed just to mark the Views to recompute??
        if (!isRestoring()) {
            for (auto* obj : getViews()) {
                auto* view = freecad_cast<DrawView*>(obj);
                if (view && view->ScaleType.isValue("Page")) {
                    if (std::abs(view->Scale.getValue() - Scale.getValue()) > std::numeric_limits<float>::epsilon()) {
                        view->Scale.setValue(Scale.getValue());
                    }
                }
            }
        }
    }
    else if (prop == &ProjectionType) {
        // touch all ortho views in the Page as they may be dependent on Projection Type  //(is this true?)
        for (auto* obj : getViews()) {
            auto* view = freecad_cast<DrawProjGroup*>(obj);
            if (view && view->ProjectionType.isValue("Default")) {
                view->ProjectionType.touch();
            }
        }

        // TODO: Also update Template graphic.
    }
    App::DocumentObject::onChanged(prop);
}

//Page is just a container. It doesn't "do" anything.
App::DocumentObjectExecReturn* DrawPage::execute(void) { return App::DocumentObject::execute(); }

// this is now irrelevant, b/c DP::execute doesn't do anything.
short DrawPage::mustExecute() const
{
    if (!isRestoring()) {
        if (
            Views.isTouched() ||
            Scale.isTouched() ||
            ProjectionType.isTouched() ||
            Template.isTouched()
        ) {
            return true;
        }
    }
    return App::DocumentObject::mustExecute();
}

PyObject* DrawPage::getPyObject(void)
{
    if (PythonObject.is(Py::_None())) {
        // ref counter is set to 1
        PythonObject = Py::Object(new DrawPagePy(this), true);
    }

    return Py::new_reference_to(PythonObject);
}

bool DrawPage::hasValidTemplate() const
{
    App::DocumentObject* obj = nullptr;
    obj = Template.getValue();

    if (obj && obj->isDerivedFrom<TechDraw::DrawTemplate>()) {
        TechDraw::DrawTemplate* templ = static_cast<TechDraw::DrawTemplate*>(obj);
        if (templ->getWidth() > 0. && templ->getHeight() > 0.) {
            return true;
        }
    }

    return false;
}

double DrawPage::getPageWidth() const
{
    App::DocumentObject* obj = Template.getValue();

    if (obj && obj->isDerivedFrom<TechDraw::DrawTemplate>()) {
        TechDraw::DrawTemplate* templ = static_cast<TechDraw::DrawTemplate*>(obj);
        return templ->getWidth();
    }

    throw Base::RuntimeError("Template not set for Page");
}

double DrawPage::getPageHeight() const
{
    App::DocumentObject* obj = Template.getValue();

    if (obj && obj->isDerivedFrom<TechDraw::DrawTemplate>()) {
        TechDraw::DrawTemplate* templ = static_cast<TechDraw::DrawTemplate*>(obj);
        return templ->getHeight();
    }

    throw Base::RuntimeError("Template not set for Page");
}

//orientation as text
const char* DrawPage::getPageOrientation() const
{
    App::DocumentObject* obj;
    obj = Template.getValue();

    if (obj && obj->isDerivedFrom<TechDraw::DrawTemplate>()) {
        TechDraw::DrawTemplate* templ = static_cast<TechDraw::DrawTemplate*>(obj);
        return templ->Orientation.getValueAsString();
    }
    throw Base::RuntimeError("Template not set for Page");
}

//orientation as 0(Portrait) or 1(Landscape)
int DrawPage::getOrientation() const
{
    App::DocumentObject* obj = Template.getValue();

    if (obj && obj->isDerivedFrom<DrawTemplate>()) {
        auto* templ = static_cast<DrawTemplate*>(obj);
        return templ->Orientation.getValue();
    }
    throw Base::RuntimeError("Template not set for Page");
}

int DrawPage::addView(App::DocumentObject* docObj, bool setPosition)
{
    if (!docObj->isDerivedFrom<DrawView>()
        && !docObj->isDerivedFrom<App::Link>()) {
        return -1;
    }

    auto* view = freecad_cast<DrawView*>(docObj);

    if (!view) {
        auto* link = dynamic_cast<App::Link*>(docObj);
        if (!link) {
            return -1;
        }

        view = freecad_cast<DrawView*>(link->getLinkedObject());
        if (!view) {
            return -1;
        }
    }

    //position all new views without owners in center of Page (exceptDVDimension)
    if (!view->claimParent()
        && !docObj->isDerivedFrom<DrawViewDimension>()
        && !docObj->isDerivedFrom<DrawViewBalloon>()
        && setPosition) {
        view->X.setValue(getPageWidth() / 2.0);
        view->Y.setValue(getPageHeight() / 2.0);
    }

    //add view to list
    std::vector<App::DocumentObject*> newViews(Views.getValues());
    newViews.push_back(docObj);
    Views.setValues(newViews);


    //check if View fits on Page
    if (!view->checkFit(this)) {
        Base::Console().warning("%s is larger than page. Will be scaled.\n",
                                view->getNameInDocument());
        view->ScaleType.setValue("Automatic");
    }

    view->checkScale();

    return Views.getSize();
}

//Note Views might be removed from document elsewhere so need to check if a View is still in Document here
int DrawPage::removeView(App::DocumentObject* docObj)
{
    if (!docObj->isDerivedFrom<DrawView>() && !docObj->isDerivedFrom<App::Link>()) {
        return -1;
    }

    App::Document* doc = docObj->getDocument();
    if (!doc) {
        return -1;
    }

    if (!docObj->isAttachedToDocument()) {
        return -1;
    }
    std::vector<App::DocumentObject*> newViews;
    for (auto* view : Views.getValues()) {
        App::Document* viewDoc = view->getDocument();
        if (!viewDoc) {
            continue;
        }

        std::string viewName = docObj->getNameInDocument();
        if (viewName.compare(view->getNameInDocument()) != 0) {
            newViews.push_back(view);
        }
    }
    Views.setValues(newViews);
    return Views.getSize();
}

void DrawPage::requestPaint(void) { signalGuiPaint(this); }

//this doesn't work right because there is no guaranteed of the restoration order
void DrawPage::onDocumentRestored()
{
    if (canUpdate()) {
        updateAllViews();
    }

    App::DocumentObject::onDocumentRestored();
}

void DrawPage::redrawCommand()
{
    //    Base::Console().message("DP::redrawCommand()\n");
    forceRedraw(true);
    updateAllViews();
    forceRedraw(false);
}

void DrawPage::updateAllViews()
{
    //    Base::Console().message("DP::updateAllViews()\n");
    //unordered list of views within page
    std::vector<App::DocumentObject*> featViews = getAllViews();

    //first, make sure all the Parts have been executed so GeometryObjects exist
    for (auto& v : featViews) {
        auto* part = freecad_cast<DrawViewPart*>(v);
        if (part) {
            //view, section, detail, dpgi
            part->recomputeFeature();
        }
    }
    //second, do the rest of the views that may depend on a part view
    //TODO: check if we have 2 layers of dependency (ex. leader > weld > tile?)
    for (auto& v : featViews) {
        auto* part = freecad_cast<DrawViewPart*>(v);
        if (part) {
            continue;
        }

        auto* view = freecad_cast<DrawView*>(v);
        if (view) {
            view->overrideKeepUpdated(true);
            view->recomputeFeature();
        }
    }
}

std::vector<App::DocumentObject*> DrawPage::getViews() const
{
    std::vector<App::DocumentObject*> views = Views.getValues();
    std::vector<App::DocumentObject*> allViews;
    for (auto& v : views) {
        bool addChildren = false;

        if (v->isDerivedFrom<App::Link>()) {
            // In the case of links, child object of the view need to be added since
            // they are not in the page Views property.
            v = static_cast<App::Link*>(v)->getLinkedObject();
            addChildren = true;
        }

        if (!v->isDerivedFrom<DrawView>()) {
            continue;
        }

        allViews.push_back(v);

        if (addChildren) {
            for (auto* dep : v->getInList()) {
                if (dep && dep->isDerivedFrom<TechDraw::DrawView>()) {
                    allViews.push_back(dep);
                }
            }
        }
    }
    return allViews;
}

std::vector<App::DocumentObject*> DrawPage::getAllViews() const
{
    std::vector<App::DocumentObject*> views = Views.getValues();
    std::vector<App::DocumentObject*> allViews;
    for (auto& v : views) {
        if (v->isDerivedFrom<App::Link>()) {
            v = static_cast<App::Link*>(v)->getLinkedObject();
        }

        if (!v->isDerivedFrom<DrawView>()) {
            continue;
        }

        allViews.push_back(v);
        if (v->isDerivedFrom<DrawProjGroup>()) {
            auto* dpg = static_cast<DrawProjGroup*>(v);
            if (dpg) {//can't really happen!
                std::vector<App::DocumentObject*> pgViews = dpg->Views.getValues();
                allViews.insert(allViews.end(), pgViews.begin(), pgViews.end());
            }
        }
    }
    return allViews;
}

void DrawPage::unsetupObject()
{
    nowUnsetting = true;

    // Remove the Page's views & template from document
    App::Document* doc = getDocument();
    std::string docName = doc->getName();
    std::string pageName = getNameInDocument();

    try {
        for (auto& v : Views.getValues()) {
            //NOTE: the order of objects in Page.Views does not reflect the object hierarchy
            //      this means that a ProjGroup could be deleted before its child ProjGroupItems.
            //      this causes problems when removing objects from document
            if (v->isAttachedToDocument()) {
                std::string viewName = v->getNameInDocument();
                Base::Interpreter().runStringArg("App.getDocument(\"%s\").removeObject(\"%s\")",
                                                 docName.c_str(), viewName.c_str());
            }
        }
        std::vector<App::DocumentObject*> emptyViews;//probably superfluous
        Views.setValues(emptyViews);
    }
    catch (...) {
        Base::Console().warning("DP::unsetupObject - %s - error while deleting children\n",
                                getNameInDocument());
    }

    App::DocumentObject* tmp = Template.getValue();
    if (tmp) {
        std::string templateName = Template.getValue()->getNameInDocument();
        Base::Interpreter().runStringArg("App.getDocument(\"%s\").removeObject(\"%s\")",
                                         docName.c_str(), templateName.c_str());
    }
    Template.setValue(nullptr);
}

int DrawPage::getNextBalloonIndex(void)
{
    int result = NextBalloonIndex.getValue();
    int newValue = result + 1;
    NextBalloonIndex.setValue(newValue);
    return result;
}

void DrawPage::handleChangedPropertyType(Base::XMLReader& reader, const char* TypeName,
                                         App::Property* prop)
{
    if (prop == &Scale) {
        App::PropertyFloat tmp;
        if (strcmp(tmp.getTypeId().getName(), TypeName) == 0) {//property in file is Float
            tmp.setContainer(this);
            tmp.Restore(reader);
            double tmpValue = tmp.getValue();
            if (tmpValue > 0.0) {
                Scale.setValue(tmpValue);
            }
            else {
                Scale.setValue(1.0);
            }
        }
    }
}

bool DrawPage::canUpdate() const
{
    if (GlobalUpdateDrawings() && KeepUpdated.getValue()) {
        return true;
    }
    else if (!GlobalUpdateDrawings() && AllowPageOverride() && KeepUpdated.getValue()) {
        return true;
    }
    return false;
}

//true if object belongs to this page
bool DrawPage::hasObject(App::DocumentObject* obj)
{
    for (auto& outObj : getOutList()) {
        //TODO: check if pointer comparison is reliable enough
        if (outObj == obj) {
            return true;
        }
    }

    return false;
}

//allow/prevent drawing updates for all Pages
bool DrawPage::GlobalUpdateDrawings(void)
{
    return Preferences::getPreferenceGroup("General")->GetBool("GlobalUpdateDrawings", true);
}

//allow/prevent a single page to update despite GlobalUpdateDrawings setting
bool DrawPage::AllowPageOverride(void)
{
    return Preferences::getPreferenceGroup("General")->GetBool("AllowPageOverride", true);
}

//! get a translated label string from the context (ex TaskActiveView), the base name (ex ActiveView) and
//! the unique name within the document (ex ActiveView001), and use it to update the Label property.
void DrawPage::translateLabel(std::string context, std::string baseName, std::string uniqueName)
{
    Label.setValue(DrawUtil::translateArbitrary(context, baseName, uniqueName));
}


// Python Drawing feature ---------------------------------------------------------

namespace App
{
/// @cond DOXERR
PROPERTY_SOURCE_TEMPLATE(TechDraw::DrawPagePython, TechDraw::DrawPage)
template<> const char* TechDraw::DrawPagePython::getViewProviderName(void) const
{
    return "TechDrawGui::ViewProviderPage";
}
/// @endcond

// explicit template instantiation
template class TechDrawExport FeaturePythonT<TechDraw::DrawPage>;
}// namespace App
