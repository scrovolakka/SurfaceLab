(function SurfaceLabResetSelectedSurface() {
    var EFFECT_MATCH_NAME = "XPK SurfaceLab";
    var META_SURFACE_ID = 500;
    var META_ANIMATION_BANK = 501;
    var MAX_CONTROLS = 16;
    var RIG_MARKER = "SurfaceLab Controller Rig;";

    function fail(message) {
        throw new Error(message);
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
        if (!layer) {
            return null;
        }
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

    function stableLayerId(layer) {
        try {
            if (layer.id !== undefined) {
                return String(layer.id);
            }
        } catch (ignored) {
        }
        return "index-" + layer.index;
    }

    function markerValue(comment, key) {
        if (!comment || comment.indexOf(RIG_MARKER) !== 0) {
            return null;
        }
        var fields = comment.split(";");
        var prefix = key + "=";
        for (var index = 0; index < fields.length; index += 1) {
            var field = fields[index].replace(/^\s+|\s+$/g, "");
            if (field.indexOf(prefix) === 0) {
                return field.substring(prefix.length);
            }
        }
        return null;
    }

    function layerForStableId(comp, requestedId) {
        for (var index = 1; index <= comp.numLayers; index += 1) {
            if (stableLayerId(comp.layer(index)) === requestedId) {
                return comp.layer(index);
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

    function clearAutomation(property) {
        if (property.canSetExpression) {
            property.expression = "";
            property.expressionEnabled = false;
        }
        for (var keyIndex = property.numKeys; keyIndex >= 1; keyIndex -= 1) {
            property.removeKey(keyIndex);
        }
    }

    function rigLayersForTarget(comp, surfaceId, sourceLayerId) {
        var prefix = RIG_MARKER + " surfaceId=" + surfaceId +
            "; sourceLayerId=" + sourceLayerId + ";";
        var layers = [];
        for (var index = 1; index <= comp.numLayers; index += 1) {
            var layer = comp.layer(index);
            if (layer.comment.indexOf(prefix) === 0) {
                layers.push(layer);
            }
        }
        return layers;
    }

    function bankFromRigLayers(layers) {
        for (var index = 0; index < layers.length; index += 1) {
            var bankText = markerValue(layers[index].comment, "bank");
            if (bankText !== null) {
                var bank = parseInt(bankText, 10);
                if (!isNaN(bank)) {
                    return bank;
                }
            }
        }
        return -1;
    }

    function removeRigLayers(layers) {
        layers.sort(function (left, right) {
            return right.index - left.index;
        });
        for (var index = 0; index < layers.length; index += 1) {
            layers[index].remove();
        }
    }

    function resolveTarget(comp, selectedLayer) {
        var directEffect = findSurfaceLabEffect(selectedLayer);
        if (directEffect) {
            var directSurfaceId = Math.round(propertyForDiskId(
                directEffect,
                META_SURFACE_ID,
                "Controller Surface ID").valueAtTime(comp.time, false));
            var directBank = Math.round(propertyForDiskId(
                directEffect,
                META_ANIMATION_BANK,
                "Controller Animation Bank").valueAtTime(comp.time, false));
            return {
                sourceLayer: selectedLayer,
                effect: directEffect,
                sourceLayerId: stableLayerId(selectedLayer),
                surfaceId: directSurfaceId,
                bank: directBank
            };
        }

        var surfaceIdText = markerValue(selectedLayer.comment, "surfaceId");
        var sourceLayerId = markerValue(selectedLayer.comment, "sourceLayerId");
        if (surfaceIdText === null || sourceLayerId === null) {
            fail(
                "Select a layer containing SurfaceLab or one of its generated " +
                "controller Nulls.");
        }
        var sourceLayer = layerForStableId(comp, sourceLayerId);
        var effect = findSurfaceLabEffect(sourceLayer);
        if (!sourceLayer || !effect) {
            fail("The SurfaceLab source layer for this controller rig was not found.");
        }
        var surfaceId = parseInt(surfaceIdText, 10);
        var layers = rigLayersForTarget(comp, surfaceId, sourceLayerId);
        var bank = bankFromRigLayers(layers);
        if (bank < 0) {
            fail("The animation bank for this controller rig was not found.");
        }
        return {
            sourceLayer: sourceLayer,
            effect: effect,
            sourceLayerId: sourceLayerId,
            surfaceId: surfaceId,
            bank: bank
        };
    }

    var comp = app.project && app.project.activeItem;
    if (!(comp instanceof CompItem)) {
        alert("Open a composition and select a SurfaceLab layer or controller Null.");
        return;
    }
    if (comp.selectedLayers.length !== 1) {
        alert("Select exactly one SurfaceLab layer or controller Null.");
        return;
    }

    var undoStarted = false;
    try {
        var target = resolveTarget(comp, comp.selectedLayers[0]);
        if (target.surfaceId < 1 || target.bank < 0 || target.bank > 7) {
            fail("SurfaceLab returned invalid controller metadata.");
        }
        var rigLayers = rigLayersForTarget(
            comp,
            target.surfaceId,
            target.sourceLayerId);
        var confirmation =
            "Reset Surface " + target.surfaceId + " to its flat control cage?\n\n" +
            "This clears its point, depth, position, rotation, scale keyframes " +
            "and expressions.";
        if (rigLayers.length > 0) {
            confirmation += "\nThe " + rigLayers.length +
                " linked controller Nulls will also be removed.";
        }
        confirmation += "\n\nThe operation can be undone once in After Effects.";
        if (!confirm(confirmation)) {
            return;
        }

        var ids = diskIdsForBank(target.bank);
        var pointProperties = [];
        var depthProperties = [];
        var rotationProperties = [];
        var scaleProperties = [];
        var index;
        for (index = 0; index < MAX_CONTROLS; index += 1) {
            pointProperties.push(propertyForDiskId(
                target.effect,
                ids.points[index]));
            depthProperties.push(propertyForDiskId(
                target.effect,
                ids.depths[index]));
        }
        for (index = 0; index < 3; index += 1) {
            rotationProperties.push(propertyForDiskId(
                target.effect,
                ids.rotations[index]));
            scaleProperties.push(propertyForDiskId(
                target.effect,
                ids.scales[index]));
        }
        var sizeXProperty = propertyForDiskId(target.effect, ids.sizeX);
        var sizeYProperty = propertyForDiskId(target.effect, ids.sizeY);
        var positionProperty = propertyForDiskId(target.effect, ids.position);

        app.beginUndoGroup("Reset SurfaceLab Selected Surface");
        undoStarted = true;

        for (index = 0; index < MAX_CONTROLS; index += 1) {
            clearAutomation(pointProperties[index]);
            clearAutomation(depthProperties[index]);
        }
        for (index = 0; index < 3; index += 1) {
            clearAutomation(rotationProperties[index]);
            clearAutomation(scaleProperties[index]);
        }
        clearAutomation(sizeXProperty);
        clearAutomation(sizeYProperty);
        clearAutomation(positionProperty);

        var width = target.sourceLayer.width;
        var height = target.sourceLayer.height;
        for (index = 0; index < MAX_CONTROLS; index += 1) {
            var row = Math.floor(index / 4);
            var column = index % 4;
            pointProperties[index].setValue([
                width * column / 3,
                height * row / 3
            ]);
            depthProperties[index].setValue(0);
        }
        for (index = 0; index < 3; index += 1) {
            rotationProperties[index].setValue(0);
            scaleProperties[index].setValue(100);
        }
        sizeXProperty.setValue(width);
        sizeYProperty.setValue(height);
        positionProperty.setValue([width / 2, height / 2, 0]);

        removeRigLayers(rigLayers);
        target.sourceLayer.selected = true;
    } catch (error) {
        alert("SurfaceLab was not reset.\n\n" + error.message);
    } finally {
        if (undoStarted) {
            app.endUndoGroup();
        }
    }
}());
