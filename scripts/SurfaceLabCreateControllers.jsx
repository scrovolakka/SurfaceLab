(function SurfaceLabCreateControllers() {
    var EFFECT_MATCH_NAME = "XPK SurfaceLab";
    var META_SURFACE_ID = 500;
    var META_ANIMATION_BANK = 501;
    var COORDINATE_SPACE = 502;
    var SCENE_POSITION = 505;
    var SCENE_ROTATIONS = [506, 507, 508];
    var SCENE_SCALES = [509, 510, 511];
    // AE and SurfaceLab agree on every per-axis rotation direction, but they
    // compose the three axes in OPPOSITE order: AE applies a layer's rotation
    // Z-then-Y-then-X to points (Orientation multiplies on afterwards, in the
    // same Z-Y-X order), while SurfaceLab's RotatePoint applies X-then-Y-
    // then-Z. Copying raw per-axis angles is therefore exact only for
    // single-axis rotations -- any compound rotation (e.g. from the viewport
    // rotate gizmo) diverges, which is why sign-map calibration could never
    // converge. The rotation bindings below instead read the Null's actual
    // rotation basis with toWorldVec and re-decompose that matrix into the
    // X-then-Y-then-Z Euler angles SurfaceLab expects; that is exact for
    // every combination of Rotation and Orientation (verified numerically
    // against AE 2026, including gimbal poses).
    var MAX_CONTROLS = 16;
    var assignedProperties = [];
    var createdLayers = [];
    var changedSetupProperties = [];

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
            originMode: 0,
            originX: 0,
            originY: 0,
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
            ids.originMode = 382;
            ids.originX = 383;
            ids.originY = 384;
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
            ids.originMode = base + 39;
            ids.originX = base + 40;
            ids.originY = base + 41;
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

    function setSetupValue(property, value) {
        if (property.value !== value) {
            changedSetupProperties.push({
                property: property,
                value: property.value
            });
            property.setValue(value);
        }
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
            "; sourceLayerId=" + sourceLayerId + ";";
        for (var index = 1; index <= comp.numLayers; index += 1) {
            var comment = comp.layer(index).comment;
            if (comment.indexOf(marker) === 0 &&
                    comment.indexOf("; role=root") >= 0) {
                return comp.layer(index);
            }
        }
        return null;
    }

    function findSceneRoot(comp, sourceLayerId) {
        var marker = "SurfaceLab Scene Rig; sourceLayerId=" +
            sourceLayerId + ";";
        for (var index = 1; index <= comp.numLayers; index += 1) {
            var layer = comp.layer(index);
            if (layer.comment.indexOf(marker) === 0 &&
                    layer.comment.indexOf("; role=scene-root") >= 0) {
                return layer;
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

    function rootLayerName(surfaceId) {
        return "SurfaceLab S" + surfaceId + " - Root";
    }

    function sceneRootLayerName() {
        return "SurfaceLab - Scene Root";
    }

    function controlLayerName(surfaceId, row, column) {
        return "SurfaceLab S" + surfaceId +
            " - Control " + row + "," + column;
    }

    function rootPositionSetup(rootName, scenePivot) {
        return "var r=thisComp.layer(" + quoteExpressionString(rootName) + ");\n" +
            "var rp=r.transform.position;\n" +
            "var sp=[" + scenePivot[0] + "," + scenePivot[1] + "," +
            scenePivot[2] + "];\n";
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

    // The surface Root null IS the pivot. This binds the plug-in's Custom
    // Origin percentage so the plug-in origin lands exactly on the Root, for
    // any Root position. Derivation: plug-in origin = Position + (pct/100 -
    // 0.5)*Size, Position = child-bounds center, and we want origin == Root, so
    // pct = 50 - 100*(childLocalBoundsCenter)/(childLocalExtent). It reads only
    // the point Nulls' LOCAL positions -- no reference to the effect's own
    // streams -- so there is no expression cycle, and it stays correct whether
    // the Root is at an edge, the centre, or anywhere the user drags it.
    function originPercentExpression(childNames, axis) {
        return "var ps=" + childPositionsArray(childNames) + ";\n" +
            "var lo=ps[0][" + axis + "],hi=lo;\n" +
            "for(var i=1;i<ps.length;i++){var v=ps[i][" + axis + "];" +
            "if(v<lo)lo=v;if(v>hi)hi=v;}\n" +
            "50-100*((lo+hi)/2)/Math.max(0.001,hi-lo);";
    }

    function positionExpression(rootName, childNames, scenePivot) {
        return rootPositionSetup(rootName, scenePivot) +
            "var ps=" + childPositionsArray(childNames) + ";\n" +
            "var minX=ps[0][0],maxX=minX,minY=ps[0][1],maxY=minY,z=0;\n" +
            "for(var i=0;i<ps.length;i++){minX=Math.min(minX,ps[i][0]);" +
            "maxX=Math.max(maxX,ps[i][0]);minY=Math.min(minY,ps[i][1]);" +
            "maxY=Math.max(maxY,ps[i][1]);z+=ps[i][2];}\n" +
            "[sp[0]+rp[0]+(minX+maxX)/2," +
            "sp[1]+rp[1]+(minY+maxY)/2," +
            "sp[2]+rp[2]+z/ps.length];";
    }

    function pointExpression(rootName, childName, scenePivot) {
        return rootPositionSetup(rootName, scenePivot) +
            "var lp=thisComp.layer(" + quoteExpressionString(childName) +
            ").transform.position;\n" +
            "[sp[0]+rp[0]+lp[0],sp[1]+rp[1]+lp[1]];";
    }

    function depthExpression(rootName, childName, scenePivot) {
        return rootPositionSetup(rootName, scenePivot) +
            "var lp=thisComp.layer(" + quoteExpressionString(childName) +
            ").transform.position;\n" +
            "sp[2]+rp[2]+lp[2];";
    }

    // Binds one SurfaceLab rotation stream to the layer's true rotation
    // matrix. The basis images of the local X/Y/Z axes are read through
    // toWorldVec (mapped back below the parent stage with fromWorldVec so the
    // scene and surface stages stay separable), the layer's own signed scale
    // is divided out, and the matrix is decomposed into the X-then-Y-then-Z
    // Euler family RotatePoint composes:
    //   M = Rz*Ry*Rx  =>  ry = asin(-M[2][0]),
    //   rx = atan2(M[2][1], M[2][2]), rz = atan2(M[1][0], M[0][0]).
    // Basis columns are renormalised so a uniformly scaled parent cannot skew
    // the asin/atan2 inputs. At the gimbal poles (|cos ry| ~ 0) rx and rz are
    // degenerate; the whole yaw goes into rx, which recomposes to the same
    // matrix.
    function rotationExpression(layerName, parentName, axis) {
        var toRelative = parentName === null ?
            "  var w=r.toWorldVec(v);\n" :
            "  var w=thisComp.layer(" + quoteExpressionString(parentName) +
                ").fromWorldVec(r.toWorldVec(v));\n";
        return "var r=thisComp.layer(" + quoteExpressionString(layerName) + ");\n" +
            "var sc=r.transform.scale;\n" +
            "function basis(v,s){\n" +
            toRelative +
            "  var n=Math.sqrt(w[0]*w[0]+w[1]*w[1]+w[2]*w[2]);\n" +
            "  if(n<1e-9)n=1e-9;\n" +
            "  if(s<0)n=-n;\n" +
            "  return [w[0]/n,w[1]/n,w[2]/n];\n" +
            "}\n" +
            "var ex=basis([1,0,0],sc[0]);\n" +
            "var ey=basis([0,1,0],sc[1]);\n" +
            "var ez=basis([0,0,1],sc[2]);\n" +
            "var sy=Math.max(-1,Math.min(1,-ex[2]));\n" +
            "var ry=Math.asin(sy),rx,rz;\n" +
            "if(Math.abs(Math.cos(ry))>1e-6){\n" +
            "  rx=Math.atan2(ey[2],ez[2]);\n" +
            "  rz=Math.atan2(ex[1],ex[0]);\n" +
            "}else{\n" +
            "  rx=Math.atan2(-ez[1],ey[1]);\n" +
            "  rz=0;\n" +
            "}\n" +
            "radiansToDegrees(" + ["rx", "ry", "rz"][axis] + ");";
    }

    // Seeds the Null rotation properties from SurfaceLab's stored angles.
    // The stored triple composes X-then-Y-then-Z (RotatePoint), an AE layer
    // composes Z-then-Y-then-X, so the matrix is rebuilt in the plug-in's
    // convention and re-decomposed into AE's; identical for single-axis
    // values, and keeps a pre-rotated surface exactly in place otherwise.
    function pluginRotationMatrix(anglesDegrees) {
        var d = Math.PI / 180;
        var cx = Math.cos(anglesDegrees[0] * d);
        var sx = Math.sin(anglesDegrees[0] * d);
        var cy = Math.cos(anglesDegrees[1] * d);
        var sy = Math.sin(anglesDegrees[1] * d);
        var cz = Math.cos(anglesDegrees[2] * d);
        var sz = Math.sin(anglesDegrees[2] * d);
        return [
            [cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
            [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
            [-sy, cy * sx, cy * cx]
        ];
    }

    function aeAnglesFromMatrix(m) {
        var b = Math.asin(Math.max(-1, Math.min(1, m[0][2])));
        var a;
        var c;
        if (Math.abs(Math.cos(b)) > 1e-6) {
            a = Math.atan2(-m[1][2], m[2][2]);
            c = Math.atan2(-m[0][1], m[0][0]);
        } else {
            a = Math.atan2(m[0][2] > 0 ? m[1][0] : -m[1][0], m[1][1]);
            c = 0;
        }
        var r = 180 / Math.PI;
        return [a * r, b * r, c * r];
    }

    function aeSeedAngles(pluginAnglesDegrees) {
        return aeAnglesFromMatrix(pluginRotationMatrix(pluginAnglesDegrees));
    }

    function scaleExpression(rootName, axis) {
        return "thisComp.layer(" + quoteExpressionString(rootName) +
            ").transform.scale[" + axis + "];";
    }

    function scenePositionExpression(rootName) {
        return "var r=thisComp.layer(" + quoteExpressionString(rootName) + ");\n" +
            "r.toWorld(r.anchorPoint);";
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
    if (sourceLayer.threeDLayer || sourceLayer.parent !== null) {
        alert(
            "Composition World controllers require SurfaceLab on an " +
            "unparented 2D host layer. Keep the rendered surface in the " +
            "3D controller rig instead of transforming the host layer.");
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
        var scenePositionProperty = propertyForDiskId(
            effect,
            SCENE_POSITION,
            "Position");
        var sceneRotationProperties = [];
        var sceneScaleProperties = [];
        var sceneRoot = findSceneRoot(comp, sourceLayerId);
        var coordinateSpaceProperty = propertyForDiskId(
            effect,
            COORDINATE_SPACE,
            "Coordinate Space");
        var cameraSourceProperty = effect.property("Camera Source");
        if (!cameraSourceProperty) {
            fail("SurfaceLab Camera Source parameter was not found.");
        }
        var pointProperties = [];
        var depthProperties = [];
        var rotationProperties = [];
        var scaleProperties = [];
        var controlledProperties = [];
        var index;
        for (index = 0; index < 3; index += 1) {
            sceneRotationProperties.push(propertyForDiskId(
                effect,
                SCENE_ROTATIONS[index]));
            sceneScaleProperties.push(propertyForDiskId(
                effect,
                SCENE_SCALES[index]));
        }
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
        var originModeProperty = propertyForDiskId(effect, ids.originMode);
        var originXProperty = propertyForDiskId(effect, ids.originX);
        var originYProperty = propertyForDiskId(effect, ids.originY);
        controlledProperties.push(sizeXProperty);
        controlledProperties.push(sizeYProperty);
        controlledProperties.push(positionProperty);
        controlledProperties.push(originXProperty);
        controlledProperties.push(originYProperty);
        ensureNoExistingAutomation(controlledProperties);
        if (!sceneRoot) {
            ensureNoExistingAutomation(
                [scenePositionProperty]
                    .concat(sceneRotationProperties)
                    .concat(sceneScaleProperties));
        }

        var initialPosition = valueAtCurrentTime(positionProperty, comp);
        var initialRotation = [];
        var initialScale = [];
        for (index = 0; index < 3; index += 1) {
            initialRotation.push(valueAtCurrentTime(rotationProperties[index], comp));
            initialScale.push(valueAtCurrentTime(scaleProperties[index], comp));
        }

        // Seat the Root on the current rotation origin (the scale/rotation
        // hinge). Mirrors BuildSurfaceCoordinateTransform: the origin lives on
        // the UNSCALED cage at position + (percent - 50%) * size.
        var initialOriginMode = valueAtCurrentTime(originModeProperty, comp);
        var initialSizeX = valueAtCurrentTime(sizeXProperty, comp);
        var initialSizeY = valueAtCurrentTime(sizeYProperty, comp);
        var originPercentX = 50;
        var originPercentY = 50;
        if (initialOriginMode === 2) {
            originPercentX = 0;
        } else if (initialOriginMode === 3) {
            originPercentX = 100;
        } else if (initialOriginMode === 4) {
            originPercentY = 0;
        } else if (initialOriginMode === 5) {
            originPercentY = 100;
        } else if (initialOriginMode === 6) {
            originPercentX = valueAtCurrentTime(originXProperty, comp);
            originPercentY = valueAtCurrentTime(originYProperty, comp);
        }
        var originPoint = [
            initialPosition[0] + (originPercentX / 100 - 0.5) * initialSizeX,
            initialPosition[1] + (originPercentY / 100 - 0.5) * initialSizeY,
            initialPosition[2]
        ];
        var initialScenePosition =
            valueAtCurrentTime(scenePositionProperty, comp);
        var initialSceneRotation = [];
        var initialSceneScale = [];
        for (index = 0; index < 3; index += 1) {
            initialSceneRotation.push(valueAtCurrentTime(
                sceneRotationProperties[index],
                comp));
            initialSceneScale.push(valueAtCurrentTime(
                sceneScaleProperties[index],
                comp));
        }
        var scenePivot = [
            sourceLayer.width / 2,
            sourceLayer.height / 2,
            0
        ];

        app.beginUndoGroup("Create SurfaceLab 3D Controllers");
        undoStarted = true;
        // AE always draws the 3D Nulls through the comp's active camera, so a
        // Comp World rig must render through it too -- including when no
        // camera exists yet: leaving the internal camera here would break
        // registration the moment a camera is added later. For exact
        // registration keep a camera layer in the comp.
        setSetupValue(cameraSourceProperty, 2);
        setSetupValue(coordinateSpaceProperty, 2);
        // Make the Root the single source of truth for the pivot: switch to
        // Custom origin, seed the percentages for the mode the user picked (so
        // the initial placement matches), then let the expressions below keep
        // the origin glued to the Root. This is what stops the null position
        // and the plug-in origin from ever drifting apart, regardless of what
        // the Rotation Origin mode was or later becomes.
        setSetupValue(originModeProperty, 6);
        setSetupValue(originXProperty, originPercentX);
        setSetupValue(originYProperty, originPercentY);

        if (!sceneRoot) {
            var sceneRootName = uniqueLayerName(
                comp,
                sceneRootLayerName());
            sceneRoot = comp.layers.addNull();
            createdLayers.push(sceneRoot);
            sceneRoot.threeDLayer = true;
            sceneRoot.name = sceneRootName;
            sceneRoot.label = 11;
            sceneRoot.comment = "SurfaceLab Scene Rig; sourceLayerId=" +
                sourceLayerId + "; role=scene-root";
            var sceneTransform = sceneRoot.property("ADBE Transform Group");
            var sceneSeed = aeSeedAngles(initialSceneRotation);
            sceneTransform.property("ADBE Anchor Point")
                .setValue([0, 0, 0]);
            sceneTransform.property("ADBE Position")
                .setValue(initialScenePosition);
            sceneTransform.property("ADBE Rotate X")
                .setValue(sceneSeed[0]);
            sceneTransform.property("ADBE Rotate Y")
                .setValue(sceneSeed[1]);
            sceneTransform.property("ADBE Rotate Z")
                .setValue(sceneSeed[2]);
            sceneTransform.property("ADBE Scale")
                .setValue(initialSceneScale);
            setExpression(
                scenePositionProperty,
                scenePositionExpression(sceneRootName));
            for (index = 0; index < 3; index += 1) {
                setExpression(
                    sceneRotationProperties[index],
                    rotationExpression(sceneRootName, null, index));
                setExpression(
                    sceneScaleProperties[index],
                    scaleExpression(sceneRootName, index));
            }
        }

        var rootName = uniqueLayerName(comp, rootLayerName(surfaceId));
        var root = comp.layers.addNull();
        createdLayers.push(root);
        root.threeDLayer = true;
        root.name = rootName;
        root.label = 9;
        root.comment = "SurfaceLab Controller Rig; surfaceId=" + surfaceId +
            "; sourceLayerId=" + sourceLayerId + "; bank=" + bank +
            "; role=root";

        root.parent = sceneRoot;
        var rootTransform = root.property("ADBE Transform Group");
        rootTransform.property("ADBE Anchor Point").setValue([0, 0, 0]);
        rootTransform.property("ADBE Position").setValue([
            originPoint[0] - scenePivot[0],
            originPoint[1] - scenePivot[1],
            originPoint[2] - scenePivot[2]
        ]);
        var rootSeed = aeSeedAngles(initialRotation);
        rootTransform.property("ADBE Rotate X")
            .setValue(rootSeed[0]);
        rootTransform.property("ADBE Rotate Y")
            .setValue(rootSeed[1]);
        rootTransform.property("ADBE Rotate Z")
            .setValue(rootSeed[2]);
        rootTransform.property("ADBE Scale")
            .setValue(initialScale);

        var childNames = [];
        for (index = 0; index < MAX_CONTROLS; index += 1) {
            var row = Math.floor(index / 4) + 1;
            var column = index % 4 + 1;
            var childName = uniqueLayerName(
                comp,
                controlLayerName(surfaceId, row, column));
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
                    pointValue[0] - originPoint[0],
                    pointValue[1] - originPoint[1],
                    depthValue - originPoint[2]
                ]);
            childNames.push(childName);
        }

        for (index = 0; index < MAX_CONTROLS; index += 1) {
            setExpression(
                pointProperties[index],
                pointExpression(rootName, childNames[index], scenePivot));
            setExpression(
                depthProperties[index],
                depthExpression(rootName, childNames[index], scenePivot));
        }
        setExpression(sizeXProperty, boundsExpression(childNames, 0));
        setExpression(sizeYProperty, boundsExpression(childNames, 1));
        setExpression(
            originXProperty,
            originPercentExpression(childNames, 0));
        setExpression(
            originYProperty,
            originPercentExpression(childNames, 1));
        setExpression(
            positionProperty,
            positionExpression(rootName, childNames, scenePivot));
        for (index = 0; index < 3; index += 1) {
            setExpression(
                rotationProperties[index],
                rotationExpression(rootName, sceneRoot.name, index));
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
        for (cleanupIndex = changedSetupProperties.length - 1;
                cleanupIndex >= 0;
                cleanupIndex -= 1) {
            try {
                changedSetupProperties[cleanupIndex].property.setValue(
                    changedSetupProperties[cleanupIndex].value);
            } catch (ignoredSetupCleanup) {
            }
        }
        alert("SurfaceLab controllers were not created.\n\n" + error.message);
    } finally {
        if (undoStarted) {
            app.endUndoGroup();
        }
    }
}());
