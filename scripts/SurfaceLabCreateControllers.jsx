(function SurfaceLabCreateControllers() {
    var EFFECT_MATCH_NAME = "XPK SurfaceLab";
    var META_SURFACE_ID = 500;
    var META_ANIMATION_BANK = 501;
    var MAX_CONTROLS = 16;
    var assignedProperties = [];
    var createdLayers = [];

    function fail(message) {
        throw new Error(message);
    }

    function quoteExpressionString(value) {
        return '"' + String(value)
            .replace(/\\/g, "\\\\")
            .replace(/"/g, '\\"')
            .replace(/\r/g, "\\r")
            .replace(/\n/g, "\\n") + '"';
    }

    function matchNameForDiskId(diskId) {
        var suffix = String(diskId);
        while (suffix.length < 4) {
            suffix = "0" + suffix;
        }
        return EFFECT_MATCH_NAME + "-" + suffix;
    }

    function propertyForDiskId(effect, diskId, displayName) {
        var property = effect.property(matchNameForDiskId(diskId));
        if (!property && displayName) {
            property = effect.property(displayName);
        }
        if (!property) {
            fail("SurfaceLab parameter not found: disk ID " + diskId);
        }
        return property;
    }

    function findSurfaceLabEffect(layer) {
        var effects = layer.property("ADBE Effect Parade");
        if (!effects) {
            return null;
        }
        for (var index = 1; index <= effects.numProperties; index += 1) {
            var effect = effects.property(index);
            if (effect.matchName === EFFECT_MATCH_NAME ||
                    effect.name === "SurfaceLab") {
                return effect;
            }
        }
        return null;
    }

    function diskIdsForBank(bank) {
        var ids = {
            points: [],
            depths: [],
            rotations: [],
            sizeX: 0,
            sizeY: 0,
            position: 0,
            scales: []
        };
        var index;
        if (bank === 0) {
            for (index = 0; index < MAX_CONTROLS; index += 1) {
                ids.points.push(100 + index);
                ids.depths.push(200 + index);
            }
            ids.rotations = [5, 6, 7];
            ids.sizeX = 308;
            ids.sizeY = 309;
            ids.position = 310;
            ids.scales = [312, 313, 314];
        } else {
            var base = 10000 + bank * 100;
            for (index = 0; index < MAX_CONTROLS; index += 1) {
                ids.points.push(base + 1 + index);
                ids.depths.push(base + 17 + index);
            }
            ids.rotations = [base + 33, base + 34, base + 35];
            ids.sizeX = base + 36;
            ids.sizeY = base + 37;
            ids.position = base + 38;
            ids.scales = [base + 42, base + 43, base + 44];
        }
        return ids;
    }

    function valueAtCurrentTime(property, comp) {
        return property.valueAtTime(comp.time, false);
    }

    function setExpression(property, expression) {
        if (!property.canSetExpression) {
            fail("Expression cannot be assigned to " + property.name + ".");
        }
        property.expression = expression;
        property.expressionEnabled = true;
        if (property.expressionError) {
            var expressionError = property.expressionError;
            property.expression = "";
            fail(property.name + ": " + expressionError);
        }
        assignedProperties.push(property);
    }

    function ensureNoExistingAutomation(properties) {
        for (var index = 0; index < properties.length; index += 1) {
            if (properties[index].expressionEnabled) {
                fail(
                    "The selected surface already has an expression on “" +
                    properties[index].name +
                    "”. The rig was not created so the expression remains untouched.");
            }
            if (properties[index].numKeys > 0) {
                fail(
                    "The selected surface already has keyframes on “" +
                    properties[index].name +
                    "”. Bake or remove them before creating this prototype rig.");
            }
        }
    }

    function stableLayerId(layer) {
        try {
            if (layer.id !== undefined) {
                return String(layer.id);
            }
        } catch (ignored) {
        }
        return "index-" + layer.index;
    }

    function findExistingRig(comp, surfaceId, sourceLayerId) {
        var marker = "SurfaceLab Controller Rig; surfaceId=" + surfaceId +
            "; sourceLayerId=" + sourceLayerId + "; role=root";
        for (var index = 1; index <= comp.numLayers; index += 1) {
            if (comp.layer(index).comment === marker) {
                return comp.layer(index);
            }
        }
        return null;
    }

    function uniqueLayerName(comp, requested) {
        if (!comp.layer(requested)) {
            return requested;
        }
        var suffix = 2;
        while (comp.layer(requested + " " + suffix)) {
            suffix += 1;
        }
        return requested + " " + suffix;
    }

    function rootLayerPositionExpression(sourceLayer, effect, positionMatchName) {
        return "var s=thisComp.layer(" + sourceLayer.index + ");\n" +
            "s.toWorld(s.effect(" + effect.propertyIndex + ")(" +
            quoteExpressionString(positionMatchName) + "));";
    }

    function rootPositionSetup(rootName) {
        return "var r=thisComp.layer(" + quoteExpressionString(rootName) + ");\n" +
            "var rw=r.toWorld(r.anchorPoint);\n" +
            "var rp=thisLayer.fromWorld(rw);\n" +
            "var rz=rp.length>2?rp[2]:rw[2];\n";
    }

    function childPositionsArray(childNames) {
        var items = [];
        for (var index = 0; index < childNames.length; index += 1) {
            items.push(
                "thisComp.layer(" + quoteExpressionString(childNames[index]) +
                ").transform.position");
        }
        return "[" + items.join(",") + "]";
    }

    function boundsExpression(childNames, axis) {
        return "var ps=" + childPositionsArray(childNames) + ";\n" +
            "var lo=ps[0][" + axis + "], hi=lo;\n" +
            "for(var i=1;i<ps.length;i++){lo=Math.min(lo,ps[i][" + axis +
            "]);hi=Math.max(hi,ps[i][" + axis + "]);}\n" +
            "Math.max(0.001,hi-lo);";
    }

    function positionExpression(rootName, childNames) {
        return rootPositionSetup(rootName) +
            "var ps=" + childPositionsArray(childNames) + ";\n" +
            "var minX=ps[0][0],maxX=minX,minY=ps[0][1],maxY=minY,z=0;\n" +
            "for(var i=0;i<ps.length;i++){minX=Math.min(minX,ps[i][0]);" +
            "maxX=Math.max(maxX,ps[i][0]);minY=Math.min(minY,ps[i][1]);" +
            "maxY=Math.max(maxY,ps[i][1]);z+=ps[i][2];}\n" +
            "[rp[0]+(minX+maxX)/2,rp[1]+(minY+maxY)/2," +
            "rz+z/ps.length];";
    }

    function pointExpression(rootName, childName) {
        return rootPositionSetup(rootName) +
            "var lp=thisComp.layer(" + quoteExpressionString(childName) +
            ").transform.position;\n" +
            "[rp[0]+lp[0],rp[1]+lp[1]];";
    }

    function depthExpression(rootName, childName) {
        return rootPositionSetup(rootName) +
            "var lp=thisComp.layer(" + quoteExpressionString(childName) +
            ").transform.position;\n" +
            "rz+lp[2];";
    }

    function rotationExpression(rootName, axis) {
        var rotationNames = ["xRotation", "yRotation", "zRotation"];
        return "var r=thisComp.layer(" + quoteExpressionString(rootName) + ");\n" +
            "r.transform.orientation[" + axis + "]+r.transform." +
            rotationNames[axis] + ";";
    }

    function scaleExpression(rootName, axis) {
        return "thisComp.layer(" + quoteExpressionString(rootName) +
            ").transform.scale[" + axis + "];";
    }

    var comp = app.project && app.project.activeItem;
    if (!(comp instanceof CompItem)) {
        alert("Open a composition and select one SurfaceLab layer.");
        return;
    }
    if (comp.selectedLayers.length !== 1) {
        alert("Select exactly one layer containing SurfaceLab.");
        return;
    }

    var sourceLayer = comp.selectedLayers[0];
    var effect = findSurfaceLabEffect(sourceLayer);
    if (!effect) {
        alert("The selected layer does not contain SurfaceLab.");
        return;
    }

    var undoStarted = false;
    try {
        var surfaceId = Math.round(valueAtCurrentTime(
            propertyForDiskId(effect, META_SURFACE_ID, "Controller Surface ID"),
            comp));
        var bank = Math.round(valueAtCurrentTime(
            propertyForDiskId(
                effect,
                META_ANIMATION_BANK,
                "Controller Animation Bank"),
            comp));
        if (surfaceId < 1 || bank < 0 || bank > 7) {
            fail("SurfaceLab returned invalid controller metadata.");
        }

        var sourceLayerId = stableLayerId(sourceLayer);
        var existing = findExistingRig(comp, surfaceId, sourceLayerId);
        if (existing) {
            fail("A controller rig already exists for this surface: " + existing.name);
        }

        var ids = diskIdsForBank(bank);
        var pointProperties = [];
        var depthProperties = [];
        var rotationProperties = [];
        var scaleProperties = [];
        var controlledProperties = [];
        var index;
        for (index = 0; index < MAX_CONTROLS; index += 1) {
            pointProperties.push(propertyForDiskId(effect, ids.points[index]));
            depthProperties.push(propertyForDiskId(effect, ids.depths[index]));
            controlledProperties.push(pointProperties[index]);
            controlledProperties.push(depthProperties[index]);
        }
        for (index = 0; index < 3; index += 1) {
            rotationProperties.push(propertyForDiskId(effect, ids.rotations[index]));
            scaleProperties.push(propertyForDiskId(effect, ids.scales[index]));
            controlledProperties.push(rotationProperties[index]);
            controlledProperties.push(scaleProperties[index]);
        }
        var sizeXProperty = propertyForDiskId(effect, ids.sizeX);
        var sizeYProperty = propertyForDiskId(effect, ids.sizeY);
        var positionProperty = propertyForDiskId(effect, ids.position);
        controlledProperties.push(sizeXProperty);
        controlledProperties.push(sizeYProperty);
        controlledProperties.push(positionProperty);
        ensureNoExistingAutomation(controlledProperties);

        var initialPosition = valueAtCurrentTime(positionProperty, comp);
        var initialRotation = [];
        var initialScale = [];
        for (index = 0; index < 3; index += 1) {
            initialRotation.push(valueAtCurrentTime(rotationProperties[index], comp));
            initialScale.push(valueAtCurrentTime(scaleProperties[index], comp));
        }

        app.beginUndoGroup("Create SurfaceLab 3D Controllers");
        undoStarted = true;

        var rootName = uniqueLayerName(comp, "SL S" + surfaceId + " Root");
        var root = comp.layers.addNull();
        createdLayers.push(root);
        root.threeDLayer = true;
        root.name = rootName;
        root.label = 9;
        root.comment = "SurfaceLab Controller Rig; surfaceId=" + surfaceId +
            "; sourceLayerId=" + sourceLayerId + "; role=root";

        var rootPosition = root.property("ADBE Transform Group")
            .property("ADBE Position");
        rootPosition.expression = rootLayerPositionExpression(
            sourceLayer,
            effect,
            positionProperty.matchName);
        if (rootPosition.expressionError) {
            fail("Unable to convert the SurfaceLab position: " +
                rootPosition.expressionError);
        }
        var initialRootWorld = rootPosition.value;
        rootPosition.expression = "";
        rootPosition.setValue(initialRootWorld);
        root.property("ADBE Transform Group").property("ADBE Rotate X")
            .setValue(initialRotation[0]);
        root.property("ADBE Transform Group").property("ADBE Rotate Y")
            .setValue(initialRotation[1]);
        root.property("ADBE Transform Group").property("ADBE Rotate Z")
            .setValue(initialRotation[2]);
        root.property("ADBE Transform Group").property("ADBE Scale")
            .setValue(initialScale);

        var childNames = [];
        for (index = 0; index < MAX_CONTROLS; index += 1) {
            var row = Math.floor(index / 4) + 1;
            var column = index % 4 + 1;
            var childName = uniqueLayerName(
                comp,
                "SL S" + surfaceId + " P" + row + "," + column);
            var child = comp.layers.addNull();
            createdLayers.push(child);
            child.threeDLayer = true;
            child.name = childName;
            child.label = 10;
            child.comment = "SurfaceLab Controller Rig; surfaceId=" + surfaceId +
                "; sourceLayerId=" + sourceLayerId + "; bank=" + bank +
                "; point=" + row + "," + column + "; role=control";
            child.parent = root;
            var pointValue = valueAtCurrentTime(pointProperties[index], comp);
            var depthValue = valueAtCurrentTime(depthProperties[index], comp);
            child.property("ADBE Transform Group").property("ADBE Position")
                .setValue([
                    pointValue[0] - initialPosition[0],
                    pointValue[1] - initialPosition[1],
                    depthValue - initialPosition[2]
                ]);
            childNames.push(childName);
        }

        for (index = 0; index < MAX_CONTROLS; index += 1) {
            setExpression(
                pointProperties[index],
                pointExpression(rootName, childNames[index]));
            setExpression(
                depthProperties[index],
                depthExpression(rootName, childNames[index]));
        }
        setExpression(sizeXProperty, boundsExpression(childNames, 0));
        setExpression(sizeYProperty, boundsExpression(childNames, 1));
        setExpression(
            positionProperty,
            positionExpression(rootName, childNames));
        for (index = 0; index < 3; index += 1) {
            setExpression(
                rotationProperties[index],
                rotationExpression(rootName, index));
            setExpression(
                scaleProperties[index],
                scaleExpression(rootName, index));
        }

        root.selected = true;
        sourceLayer.selected = false;
        for (index = 1; index <= comp.numLayers; index += 1) {
            var layer = comp.layer(index);
            if (layer !== root && layer.comment.indexOf(
                    "SurfaceLab Controller Rig; surfaceId=" + surfaceId +
                    "; sourceLayerId=" + sourceLayerId) === 0) {
                layer.selected = false;
            }
        }
    } catch (error) {
        var cleanupIndex;
        for (cleanupIndex = assignedProperties.length - 1;
                cleanupIndex >= 0;
                cleanupIndex -= 1) {
            try {
                assignedProperties[cleanupIndex].expression = "";
            } catch (ignoredExpressionCleanup) {
            }
        }
        for (cleanupIndex = createdLayers.length - 1;
                cleanupIndex >= 0;
                cleanupIndex -= 1) {
            try {
                createdLayers[cleanupIndex].remove();
            } catch (ignoredLayerCleanup) {
            }
        }
        alert("SurfaceLab controllers were not created.\n\n" + error.message);
    } finally {
        if (undoStarted) {
            app.endUndoGroup();
        }
    }
}());
