// Collects everything needed to debug SurfaceLab controller-rig registration:
// stream values, expression states and errors, rig layer transforms, and
// numeric consistency checks between the rig and the effect parameters.
// Select the SurfaceLab layer (or any rig Null) and run; the report opens in a
// copyable text box.
(function SurfaceLabDiagnoseRig() {
    var EFFECT_MATCH_NAME = "XPK SurfaceLab";
    var META_SURFACE_ID = 500;
    var META_ANIMATION_BANK = 501;
    var COORDINATE_SPACE = 502;
    var SCENE_POSITION = 505;
    var SCENE_ROTATIONS = [506, 507, 508];
    var SCENE_SCALES = [509, 510, 511];
    var ROTATION_AXIS_SIGNS = [-1, -1, -1];
    var MAX_CONTROLS = 16;
    var lines = [];

    function out(text) {
        lines.push(text);
    }

    // Rig markers store layer.id (stable across reordering); never use
    // layer.index for lookups -- rig creation inserts layers above the host
    // and shifts every index.
    function stableLayerId(layer) {
        try {
            if (layer.id !== undefined) {
                return String(layer.id);
            }
        } catch (ignored) {
        }
        return "index-" + layer.index;
    }

    function matchNameForDiskId(diskId) {
        var suffix = String(diskId);
        while (suffix.length < 4) {
            suffix = "0" + suffix;
        }
        return EFFECT_MATCH_NAME + "-" + suffix;
    }

    function propertyForDiskId(effect, diskId) {
        return effect.property(matchNameForDiskId(diskId));
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

    function round2(value) {
        return Math.round(value * 100) / 100;
    }

    function formatValue(value) {
        if (value instanceof Array) {
            var parts = [];
            for (var index = 0; index < value.length; index += 1) {
                parts.push(String(round2(value[index])));
            }
            return "[" + parts.join(", ") + "]";
        }
        return String(round2(value));
    }

    function reportStream(label, property) {
        if (!property) {
            out("  " + label + ": (PARAM NOT FOUND)");
            return null;
        }
        var line = "  " + label + " = " + formatValue(property.value);
        if (property.expressionEnabled) {
            line += "  [expr ON]";
            if (property.expressionError) {
                line += "  !! EXPR ERROR: " + property.expressionError;
            }
        } else if (property.expression && property.expression.length > 0) {
            line += "  [expr present but DISABLED]";
        }
        out(line);
        return property;
    }

    function findRigLayer(comp, marker, role) {
        for (var index = 1; index <= comp.numLayers; index += 1) {
            var layer = comp.layer(index);
            if (layer.comment.indexOf(marker) === 0 &&
                    layer.comment.indexOf("; role=" + role) >= 0) {
                return layer;
            }
        }
        return null;
    }

    function countRigLayers(comp, marker, role) {
        var count = 0;
        for (var index = 1; index <= comp.numLayers; index += 1) {
            var layer = comp.layer(index);
            if (layer.comment.indexOf(marker) === 0 &&
                    layer.comment.indexOf("; role=" + role) >= 0) {
                count += 1;
            }
        }
        return count;
    }

    function check(label, condition, detail) {
        out("  [" + (condition ? "PASS" : "FAIL") + "] " + label +
            (detail ? "  (" + detail + ")" : ""));
    }

    var comp = app.project && app.project.activeItem;
    if (!(comp instanceof CompItem)) {
        alert("Open a composition first.");
        return;
    }

    // Resolve the SurfaceLab layer: selection first, then comp scan.
    var sourceLayer = null;
    var effect = null;
    var index;
    for (index = 0; index < comp.selectedLayers.length; index += 1) {
        effect = findSurfaceLabEffect(comp.selectedLayers[index]);
        if (effect) {
            sourceLayer = comp.selectedLayers[index];
            break;
        }
    }
    if (!effect) {
        for (index = 1; index <= comp.numLayers; index += 1) {
            effect = findSurfaceLabEffect(comp.layer(index));
            if (effect) {
                sourceLayer = comp.layer(index);
                break;
            }
        }
    }
    if (!effect) {
        alert("No SurfaceLab effect found in this composition.");
        return;
    }

    out("=== SurfaceLab Rig Diagnostic ===");
    out("Comp: " + comp.name + "  (" + comp.width + "x" + comp.height + ")");
    out("Host layer: " + sourceLayer.name +
        "  (" + sourceLayer.width + "x" + sourceLayer.height +
        ", 3D=" + sourceLayer.threeDLayer +
        ", parented=" + (sourceLayer.parent !== null) + ")");
    // The Comp World mode cancels the host layer's affine transform; that
    // cancel assumes an unparented 2D layer. Any scale/rotation/offset here
    // sits between the render and the Null rig, so report it verbatim.
    var hostTransform = sourceLayer.property("ADBE Transform Group");
    out("Host layer transform: position=" +
        formatValue(hostTransform.property("ADBE Position").value) +
        " scale=" + formatValue(hostTransform.property("ADBE Scale").value) +
        " rotation=" + round2(hostTransform.property("ADBE Rotate Z").value) +
        " anchor=" + formatValue(
            hostTransform.property("ADBE Anchor Point").value));
    out("Active camera: " +
        (comp.activeCamera ? comp.activeCamera.name : "(none)"));
    out("");

    var surfaceIdProperty = propertyForDiskId(effect, META_SURFACE_ID);
    var bankProperty = propertyForDiskId(effect, META_ANIMATION_BANK);
    var surfaceId = surfaceIdProperty ? surfaceIdProperty.value : -1;
    var bank = bankProperty ? bankProperty.value : -1;
    out("Selected surface id=" + surfaceId + "  bank=" + bank);
    var coordinateSpace = propertyForDiskId(effect, COORDINATE_SPACE);
    out("Coordinate Space = " +
        (coordinateSpace ? coordinateSpace.value : "?") +
        "  (1=Layer Local, 2=Comp World)");
    var cameraSource = propertyForDiskId(effect, 407);
    out("Camera Source = " +
        (cameraSource ? cameraSource.value : "?") +
        "  (1=Internal, 2=AE Active Camera)");
    var lightSource = propertyForDiskId(effect, 408);
    out("Light Source = " +
        (lightSource ? lightSource.value : "?") +
        "  (1=Internal, 2=AE Comp Lights)");
    out("");

    out("Scene Transform streams:");
    var scenePositionProperty = reportStream(
        "Scene Position",
        propertyForDiskId(effect, SCENE_POSITION));
    var sceneRotationProperties = [];
    var sceneScaleProperties = [];
    for (index = 0; index < 3; index += 1) {
        sceneRotationProperties.push(reportStream(
            "Scene Rotation " + "XYZ".charAt(index),
            propertyForDiskId(effect, SCENE_ROTATIONS[index])));
        sceneScaleProperties.push(reportStream(
            "Scene Scale " + "XYZ".charAt(index),
            propertyForDiskId(effect, SCENE_SCALES[index])));
    }
    out("");

    var ids = diskIdsForBank(bank);
    out("Surface streams (bank " + bank + "):");
    var positionProperty = reportStream(
        "Position",
        propertyForDiskId(effect, ids.position));
    var sizeXProperty = reportStream(
        "Size X",
        propertyForDiskId(effect, ids.sizeX));
    var sizeYProperty = reportStream(
        "Size Y",
        propertyForDiskId(effect, ids.sizeY));
    var rotationProperties = [];
    var scaleProperties = [];
    for (index = 0; index < 3; index += 1) {
        rotationProperties.push(reportStream(
            "Rotation " + "XYZ".charAt(index),
            propertyForDiskId(effect, ids.rotations[index])));
    }
    for (index = 0; index < 3; index += 1) {
        scaleProperties.push(reportStream(
            "Scale " + "XYZ".charAt(index),
            propertyForDiskId(effect, ids.scales[index])));
    }
    var originModeProperty = reportStream(
        "Origin Mode",
        propertyForDiskId(effect, ids.originMode));
    var originXProperty = reportStream(
        "Origin X %",
        propertyForDiskId(effect, ids.originX));
    var originYProperty = reportStream(
        "Origin Y %",
        propertyForDiskId(effect, ids.originY));
    reportStream("Point 1,1", propertyForDiskId(effect, ids.points[0]));
    reportStream("Point 4,4", propertyForDiskId(effect, ids.points[15]));
    out("");

    // Rig layers.
    var sourceLayerId = stableLayerId(sourceLayer);
    var sceneMarker = "SurfaceLab Scene Rig; sourceLayerId=" +
        sourceLayerId + ";";
    var rigMarker = "SurfaceLab Controller Rig; surfaceId=" + surfaceId +
        "; sourceLayerId=" + sourceLayerId;
    var sceneRoot = findRigLayer(comp, sceneMarker, "scene-root");
    var surfaceRoot = findRigLayer(comp, rigMarker, "root");
    var controlCount = countRigLayers(comp, rigMarker, "control");
    out("Rig layers (host layer id=" + sourceLayerId + "):");
    out("  Scene Root: " + (sceneRoot ? sceneRoot.name : "(none)"));
    out("  Surface Root: " + (surfaceRoot ? surfaceRoot.name : "(none)"));
    out("  Control nulls: " + controlCount + " / " + MAX_CONTROLS);
    // Any marked layer in the comp, matched or not -- makes stale rigs from
    // other layers/ids and duplicates visible.
    out("  All SurfaceLab-marked layers in comp:");
    var markedCount = 0;
    for (index = 1; index <= comp.numLayers; index += 1) {
        var markedLayer = comp.layer(index);
        if (markedLayer.comment.indexOf("SurfaceLab") === 0) {
            markedCount += 1;
            out("    " + markedLayer.name + "  {" +
                markedLayer.comment + "}");
        }
    }
    if (markedCount === 0) {
        out("    (none)");
    }
    out("");

    // The actually-bound expression sign per rotation axis, read from the
    // expression text itself. This cannot be fooled by zero rotations, unlike
    // the value comparison below.
    function boundSign(property) {
        if (!property || !property.expressionEnabled) {
            return "(no expression)";
        }
        var text = String(property.expression);
        if (text.indexOf("(-1)*(") >= 0) {
            return "-1";
        }
        if (text.indexOf("(1)*(") >= 0) {
            return "+1";
        }
        return "raw (no sign factor; pre-sign-map rig)";
    }
    out("Bound rotation-expression signs (from expression text):");
    for (index = 0; index < 3; index += 1) {
        out("  Surface Rotation " + "XYZ".charAt(index) + ": " +
            boundSign(rotationProperties[index]));
    }
    for (index = 0; index < 3; index += 1) {
        out("  Scene Rotation " + "XYZ".charAt(index) + ": " +
            boundSign(sceneRotationProperties[index]));
    }
    out("  (this script's expected map: [" +
        ROTATION_AXIS_SIGNS.join(", ") + "])");
    // Raw dump removes all ambiguity about what is actually baked into the rig.
    if (rotationProperties[1] && rotationProperties[1].expressionEnabled) {
        out("  Raw Surface Rotation Y expression:");
        out("    " + String(rotationProperties[1].expression)
            .replace(/\n/g, " \\n "));
    }
    out("");

    // Consistency checks.
    out("Consistency checks:");
    var allRotationsZero = true;
    for (index = 0; index < 3; index += 1) {
        if (rotationProperties[index] &&
                Math.abs(rotationProperties[index].value) > 0.01) {
            allRotationsZero = false;
        }
    }
    if (allRotationsZero) {
        out("  [NOTE] All surface rotations are 0, so the rotation-binding " +
            "value checks below are vacuous. Set the Surface Root's " +
            "Y Rotation to +30 and re-run for a live check.");
    }
    if (coordinateSpace && cameraSource) {
        check(
            "Comp World renders through the AE camera",
            !(coordinateSpace.value === 2 && cameraSource.value !== 2),
            "AE draws 3D Nulls through the comp camera; with the internal " +
            "camera the render and rig cannot register");
    }
    if (cameraSource && cameraSource.value === 2 && !comp.activeCamera) {
        out("  [WARN] Camera Source is AE Active Camera but the comp has no " +
            "camera layer; add one for exact registration.");
    }
    if (surfaceRoot && positionProperty && sizeXProperty && sizeYProperty &&
            originModeProperty && originXProperty && originYProperty) {
        var scenePivot = [
            sourceLayer.width / 2,
            sourceLayer.height / 2,
            0
        ];
        var mode = originModeProperty.value;
        var px = 50;
        var py = 50;
        if (mode === 2) {
            px = 0;
        } else if (mode === 3) {
            px = 100;
        } else if (mode === 4) {
            py = 0;
        } else if (mode === 5) {
            py = 100;
        } else if (mode === 6) {
            px = originXProperty.value;
            py = originYProperty.value;
        }
        var position = positionProperty.value;
        var expectedOrigin = [
            position[0] + (px / 100 - 0.5) * sizeXProperty.value,
            position[1] + (py / 100 - 0.5) * sizeYProperty.value,
            position[2]
        ];
        // Compare in Scene-Root-local space: root local must equal
        // origin - scenePivot. This stays valid however the Scene Root has
        // been moved, rotated, or scaled, since locals are unaffected.
        var rootPosition = surfaceRoot.property("ADBE Transform Group")
            .property("ADBE Position").value;
        var expectedLocal = [
            expectedOrigin[0] - scenePivot[0],
            expectedOrigin[1] - scenePivot[1],
            expectedOrigin[2] - scenePivot[2]
        ];
        var originDelta = Math.sqrt(
            Math.pow(rootPosition[0] - expectedLocal[0], 2) +
            Math.pow(rootPosition[1] - expectedLocal[1], 2) +
            Math.pow(rootPosition[2] - expectedLocal[2], 2));
        check(
            "Surface Root sits on the rotation origin",
            originDelta < 1.0,
            "root(local)=" + formatValue(rootPosition) +
            " vs origin-pivot=" + formatValue(expectedLocal) +
            " delta=" + round2(originDelta));

        var rootTransform = surfaceRoot.property("ADBE Transform Group");
        var rootScale = rootTransform.property("ADBE Scale").value;
        for (index = 0; index < 3; index += 1) {
            if (scaleProperties[index]) {
                check(
                    "Scale " + "XYZ".charAt(index) + " bound to root",
                    Math.abs(scaleProperties[index].value -
                        rootScale[index]) < 0.5,
                    "param=" + round2(scaleProperties[index].value) +
                    " root=" + round2(rootScale[index]));
            }
        }
        var rootRotations = [
            rootTransform.property("ADBE Rotate X").value +
                rootTransform.property("ADBE Orientation").value[0],
            rootTransform.property("ADBE Rotate Y").value +
                rootTransform.property("ADBE Orientation").value[1],
            rootTransform.property("ADBE Rotate Z").value +
                rootTransform.property("ADBE Orientation").value[2]
        ];
        for (index = 0; index < 3; index += 1) {
            if (rotationProperties[index]) {
                check(
                    "Rotation " + "XYZ".charAt(index) +
                    " bound with sign " + ROTATION_AXIS_SIGNS[index],
                    Math.abs(rotationProperties[index].value -
                        ROTATION_AXIS_SIGNS[index] * rootRotations[index]) < 0.5,
                    "param=" + round2(rotationProperties[index].value) +
                    " root=" + round2(rootRotations[index]));
            }
        }
    } else {
        out("  (skipped: no surface root or missing streams)");
    }
    if (sceneRoot && scenePositionProperty) {
        // toWorld only exists in the expression language, not ExtendScript;
        // the Scene Root is unparented, so its position IS its world position.
        var sceneRootWorld = sceneRoot.property("ADBE Transform Group")
            .property("ADBE Position").value;
        var scenePositionValue = scenePositionProperty.value;
        var sceneDelta = Math.sqrt(
            Math.pow(sceneRootWorld[0] - scenePositionValue[0], 2) +
            Math.pow(sceneRootWorld[1] - scenePositionValue[1], 2) +
            Math.pow(sceneRootWorld[2] - scenePositionValue[2], 2));
        check(
            "Scene Position bound to Scene Root",
            sceneDelta < 1.0,
            "root=" + formatValue(sceneRootWorld) +
            " param=" + formatValue(scenePositionValue));
    }

    var report = lines.join("\n");
    var window = new Window("dialog", "SurfaceLab Rig Diagnostic");
    var box = window.add("edittext", undefined, report, {
        multiline: true,
        scrolling: true
    });
    box.preferredSize = [640, 480];
    window.add("button", undefined, "OK");
    window.show();
})();
