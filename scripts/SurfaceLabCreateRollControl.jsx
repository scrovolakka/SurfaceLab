/*
 * SurfaceLab Roll control issuer
 *
 * Creates one ordinary AE Null for a surface's procedural Roll controls and
 * connects the SurfaceLab parameters through expressions. A Layer Control on
 * the host keeps the link stable when the Null is renamed or reordered.
 */
(function surfaceLabCreateRollControl() {
    var EFFECT_MATCH_NAME = "XPK SurfaceLab";
    var SURFACE_COUNT = 8;
    var SURFACE_STRIDE = 26;
    var SURFACE_PARAMETERS_START = 11;
    var SURFACE_ROLL_ANGLE_OFFSET = 12;
    var SURFACE_ROLL_TILT_OFFSET = 13;
    var SURFACE_ROLL_RADIUS_OFFSET = 14;
    var SURFACE_ROLL_EXPAND_OFFSET = 15;
    var RIG_SURFACE = 226;
    var RIG_ROW = 227;
    var RIG_SURFACE_ID_0 = 228;

    function surfacePropertyIndex(surfaceIndex, offset) {
        return SURFACE_PARAMETERS_START +
            surfaceIndex * SURFACE_STRIDE + offset;
    }

    function findHost(comp) {
        var selected = comp.selectedLayers;
        var layerIndex;
        var effectIndex;
        for (layerIndex = 0;
             layerIndex < selected.length;
             layerIndex += 1) {
            var selectedEffects =
                selected[layerIndex].property("ADBE Effect Parade");
            if (!selectedEffects) {
                continue;
            }
            for (effectIndex = 1;
                 effectIndex <= selectedEffects.numProperties;
                 effectIndex += 1) {
                if (selectedEffects.property(effectIndex).matchName ===
                    EFFECT_MATCH_NAME) {
                    return {
                        layer: selected[layerIndex],
                        effect: selectedEffects.property(effectIndex)
                    };
                }
            }
        }
        for (layerIndex = 1;
             layerIndex <= comp.numLayers;
             layerIndex += 1) {
            var effects =
                comp.layer(layerIndex).property("ADBE Effect Parade");
            if (!effects) {
                continue;
            }
            for (effectIndex = 1;
                 effectIndex <= effects.numProperties;
                 effectIndex += 1) {
                if (effects.property(effectIndex).matchName ===
                    EFFECT_MATCH_NAME) {
                    return {
                        layer: comp.layer(layerIndex),
                        effect: effects.property(effectIndex)
                    };
                }
            }
        }
        return null;
    }

    function surfaceIdentity(effect, surfaceIndex) {
        var rigSurface = effect.property(RIG_SURFACE);
        var rigRow = effect.property(RIG_ROW);
        if (!rigSurface || !rigRow ||
            !effect.property(RIG_SURFACE_ID_0 + 3)) {
            throw new Error(
                "The loaded SurfaceLab build has no Null Rig Bridge. " +
                "Install the current plug-in build and restart After Effects.");
        }
        rigSurface.setValue(surfaceIndex + 1);
        rigRow.setValue(1);
        rigRow.setValue(0);
        var identity = "";
        var chunk;
        for (chunk = 0; chunk < 4; chunk += 1) {
            identity += "|id" + chunk + "=" + parseInt(
                effect.property(RIG_SURFACE_ID_0 + chunk).value,
                10);
        }
        return identity;
    }

    function markerComments(layer) {
        var result = [];
        var markers = layer.property("ADBE Marker");
        if (!markers) {
            return result;
        }
        var key;
        for (key = 1; key <= markers.numKeys; key += 1) {
            result.push(markers.keyValue(key).comment);
        }
        return result;
    }

    function findMarkedLayer(comp, identity) {
        var layerIndex;
        for (layerIndex = 1;
             layerIndex <= comp.numLayers;
             layerIndex += 1) {
            var comments = markerComments(comp.layer(layerIndex));
            var commentIndex;
            for (commentIndex = 0;
                 commentIndex < comments.length;
                 commentIndex += 1) {
                if (comments[commentIndex] === identity) {
                    return comp.layer(layerIndex);
                }
            }
        }
        return null;
    }

    function setIdentityMarker(layer, identity) {
        if (findMarkedLayer(layer.containingComp, identity) === layer) {
            return;
        }
        layer.property("ADBE Marker").setValueAtTime(
            0,
            new MarkerValue(identity));
    }

    function findEffectByName(effects, name) {
        var index;
        for (index = 1; index <= effects.numProperties; index += 1) {
            if (effects.property(index).name === name) {
                return effects.property(index);
            }
        }
        return null;
    }

    function findEffectByMatchName(effects, matchName) {
        var index;
        for (index = 1; index <= effects.numProperties; index += 1) {
            if (effects.property(index).matchName === matchName) {
                return effects.property(index);
            }
        }
        return null;
    }

    function getOrCreateControlEffect(
        effects,
        matchName,
        name) {
        var control = findEffectByName(effects, name);
        if (!control) {
            control = effects.addProperty(matchName);
            if (!control) {
                throw new Error(
                    "After Effects could not create " + name + ".");
            }
            control.name = name;
        }
        return control;
    }

    function copyKeysOrValue(source, destination) {
        if (destination.numKeys > 0) {
            return;
        }
        if (source.numKeys > 0) {
            var key;
            for (key = 1; key <= source.numKeys; key += 1) {
                destination.setValueAtTime(
                    source.keyTime(key),
                    source.keyValue(key));
            }
        } else {
            destination.setValue(source.value);
        }
    }

    function showDialog() {
        var dialog = new Window(
            "dialog",
            "SurfaceLab v1 — Roll Control");
        dialog.orientation = "column";
        dialog.alignChildren = ["fill", "top"];

        var group = dialog.add("group");
        group.add("statictext", undefined, "Surface");
        var surface = group.add(
            "dropdownlist",
            undefined,
            ["1", "2", "3", "4", "5", "6", "7", "8"]);
        surface.selection = 0;

        var note = dialog.add(
            "statictext",
            undefined,
            "Existing Roll key values are copied to a new controller.",
            {multiline: true});
        note.preferredSize.width = 330;

        var buttons = dialog.add("group");
        buttons.alignment = "right";
        buttons.add("button", undefined, "Cancel", {name: "cancel"});
        buttons.add("button", undefined, "Create", {name: "ok"});

        if (dialog.show() !== 1) {
            return null;
        }
        return {surface: surface.selection.index};
    }

    var comp = app.project && app.project.activeItem;
    if (!(comp instanceof CompItem)) {
        alert("SurfaceLab v1\n\nOpen or select a composition first.");
        return;
    }
    var host = findHost(comp);
    if (!host) {
        alert(
            "SurfaceLab v1\n\nSelect a layer with SurfaceLab applied.");
        return;
    }
    var options = showDialog();
    if (!options) {
        return;
    }

    app.beginUndoGroup("Create SurfaceLab Roll Control");
    try {
        var identity =
            "SurfaceLabV1|roll-control|host=" +
            String(host.layer.id) +
            surfaceIdentity(host.effect, options.surface);
        var controller = findMarkedLayer(comp, identity);
        var controllerIsNew = !controller;
        if (!controller) {
            controller = comp.layers.addNull(comp.duration);
            controller.name =
                "SL S" + (options.surface + 1) + " Roll";
            controller.label = 10;
            controller.threeDLayer = false;
            controller.property("ADBE Transform Group")
                .property("ADBE Position")
                .setValue([120, 120]);
            setIdentityMarker(controller, identity);
        }

        var controllerEffects =
            controller.property("ADBE Effect Parade");
        getOrCreateControlEffect(
            controllerEffects,
            "ADBE Angle Control",
            "Roll Angle");
        getOrCreateControlEffect(
            controllerEffects,
            "ADBE Angle Control",
            "Roll Tilt");
        getOrCreateControlEffect(
            controllerEffects,
            "ADBE Slider Control",
            "Roll Radius");
        getOrCreateControlEffect(
            controllerEffects,
            "ADBE Slider Control",
            "Roll Expand / Turn");

        // Adding an effect invalidates previously obtained indexed child
        // references in AE scripting. Reacquire all controls after the last
        // addProperty call.
        var angle = findEffectByName(
            controllerEffects,
            "Roll Angle");
        var tilt = findEffectByName(
            controllerEffects,
            "Roll Tilt");
        var radius = findEffectByName(
            controllerEffects,
            "Roll Radius");
        var expand = findEffectByName(
            controllerEffects,
            "Roll Expand / Turn");

        var linkName =
            "SL S" + (options.surface + 1) + " Roll Controller";
        var hostEffects =
            host.layer.property("ADBE Effect Parade");
        var layerControl = getOrCreateControlEffect(
            hostEffects,
            "ADBE Layer Control",
            linkName);
        layerControl.property(1).setValue(controller.index);

        // The host Effect Parade changed as well, so reacquire SurfaceLab
        // before addressing its Roll streams.
        var surfaceLabEffect = findEffectByMatchName(
            hostEffects,
            EFFECT_MATCH_NAME);
        if (!surfaceLabEffect) {
            throw new Error(
                "SurfaceLab became unavailable after creating the Layer Control.");
        }
        var sourceProperties = [
            surfaceLabEffect.property(surfacePropertyIndex(
                options.surface,
                SURFACE_ROLL_ANGLE_OFFSET)),
            surfaceLabEffect.property(surfacePropertyIndex(
                options.surface,
                SURFACE_ROLL_TILT_OFFSET)),
            surfaceLabEffect.property(surfacePropertyIndex(
                options.surface,
                SURFACE_ROLL_RADIUS_OFFSET)),
            surfaceLabEffect.property(surfacePropertyIndex(
                options.surface,
                SURFACE_ROLL_EXPAND_OFFSET))
        ];
        var controls = [
            angle.property(1),
            tilt.property(1),
            radius.property(1),
            expand.property(1)
        ];
        if (controllerIsNew) {
            var copyIndex;
            for (copyIndex = 0;
                 copyIndex < controls.length;
                 copyIndex += 1) {
                copyKeysOrValue(
                    sourceProperties[copyIndex],
                    controls[copyIndex]);
            }
        }

        var controlNames = [
            "Roll Angle",
            "Roll Tilt",
            "Roll Radius",
            "Roll Expand / Turn"
        ];
        var expressionIndex;
        for (expressionIndex = 0;
             expressionIndex < sourceProperties.length;
             expressionIndex += 1) {
            if (!sourceProperties[expressionIndex].canSetExpression) {
                throw new Error(
                    "SurfaceLab Roll parameter cannot accept expressions.");
            }
            sourceProperties[expressionIndex].expression =
                "var c = thisLayer.effect(\"" +
                linkName + "\")(1);\n" +
                "c ? c.effect(\"" +
                controlNames[expressionIndex] + "\")(1) : value;";
        }

        var layerIndex;
        for (layerIndex = 1;
             layerIndex <= comp.numLayers;
             layerIndex += 1) {
            comp.layer(layerIndex).selected = false;
        }
        controller.selected = true;
        alert(
            "SurfaceLab v1\n\nRoll controller ready for Surface " +
            (options.surface + 1) + ".");
    } catch (error) {
        alert("SurfaceLab Roll Control\n\n" + error.toString());
    } finally {
        app.endUndoGroup();
    }
}());
