/*
 * SurfaceLab shared-edge helper
 *
 * Run SurfaceLabCreateNullRig.jsx first and issue the two edge columns/rows
 * that should meet. This helper merges each point pair onto the Surface A
 * Null by adding both point identities to that layer's markers. SurfaceLab
 * then resolves the one animated Null into both lattice vertices.
 */
(function surfaceLabChainEdges() {
    var EFFECT_MATCH_NAME = "XPK SurfaceLab";
    var SURFACE_COUNT = 8;
    var SURFACE_STRIDE = 26;
    var SURFACE_PARAMETERS_START = 11;
    var SURFACE_SOURCE_OFFSET = 1;
    var SURFACE_IMAGE_TRANSFORM_OFFSET = 23;
    var SURFACE_IMAGE_SCALE_OFFSET = 24;
    var RIG_REQUEST = 225;
    var RIG_METADATA = 226;

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

    function surfaceInfo(effect, surfaceIndex) {
        var rigRequest = effect.property(RIG_REQUEST);
        var rigMetadata = effect.property(RIG_METADATA);
        if (!rigRequest || !rigMetadata) {
            throw new Error(
                "The loaded SurfaceLab build has no Null Rig Bridge. " +
                "Install the current plug-in build and restart After Effects.");
        }
        rigRequest.setValue([surfaceIndex + 1, 1, 0]);
        rigRequest.setValue([surfaceIndex + 1, 0, 0]);
        var metadata = rigMetadata.value;
        var idHigh = Math.round(Number(metadata[0]));
        var idLow = Math.round(Number(metadata[1]));
        var packedDimensions = Math.round(Number(metadata[2]));
        var chunks = [
            Math.floor(idHigh / 65536),
            idHigh % 65536,
            Math.floor(idLow / 65536),
            idLow % 65536
        ];
        var dx = Math.floor(packedDimensions / 32);
        var dy = packedDimensions % 32;
        if (!isFinite(idHigh) || !isFinite(idLow) ||
            idHigh < 0 || idHigh > 4294967295 ||
            idLow < 0 || idLow > 4294967295 ||
            isNaN(dx) || isNaN(dy) ||
            dx < 1 || dx > 16 || dy < 1 || dy > 16) {
            throw new Error(
                "SurfaceLab did not publish valid lattice metadata.");
        }
        return {
            chunks: chunks,
            divisionsX: dx,
            divisionsY: dy
        };
    }

    function pointComment(hostId, info, row, column) {
        return "SurfaceLabV1|point|host=" + hostId +
            "|id0=" + info.chunks[0] +
            "|id1=" + info.chunks[1] +
            "|id2=" + info.chunks[2] +
            "|id3=" + info.chunks[3] +
            "|row=" + row +
            "|col=" + column;
    }

    function markerHasComment(layer, comment) {
        var markers = layer.property("ADBE Marker");
        if (!markers) {
            return false;
        }
        var key;
        for (key = 1; key <= markers.numKeys; key += 1) {
            if (markers.keyValue(key).comment === comment) {
                return true;
            }
        }
        return false;
    }

    function findLayerWithMarker(comp, comment) {
        var index;
        for (index = 1; index <= comp.numLayers; index += 1) {
            if (markerHasComment(comp.layer(index), comment)) {
                return comp.layer(index);
            }
        }
        return null;
    }

    function addMarkerComment(layer, comment, comp) {
        if (markerHasComment(layer, comment)) {
            return;
        }
        var markers = layer.property("ADBE Marker");
        var time = 0;
        if (markers.numKeys > 0) {
            time = Math.min(
                comp.duration * 0.5,
                Math.max(0.0001, comp.frameDuration * 0.5));
            while (markers.nearestKeyIndex(time) <= markers.numKeys &&
                   Math.abs(
                       markers.keyTime(markers.nearestKeyIndex(time)) -
                       time) < 0.000001) {
                time += Math.max(0.0001, comp.frameDuration * 0.1);
                if (time >= comp.duration) {
                    time = comp.duration * 0.75;
                    break;
                }
            }
        }
        markers.setValueAtTime(time, new MarkerValue(comment));
    }

    function removeMarkerComment(layer, comment) {
        var markers = layer.property("ADBE Marker");
        if (!markers) {
            return;
        }
        var key;
        for (key = markers.numKeys; key >= 1; key -= 1) {
            if (markers.keyValue(key).comment === comment) {
                markers.removeKey(key);
            }
        }
    }

    function edgePoints(info, edge) {
        var points = [];
        var row;
        var column;
        if (edge === 0 || edge === 1) {
            column = edge === 0 ? 0 : info.divisionsX;
            for (row = 0; row <= info.divisionsY; row += 1) {
                points.push({row: row, column: column});
            }
        } else {
            row = edge === 2 ? 0 : info.divisionsY;
            for (column = 0;
                 column <= info.divisionsX;
                 column += 1) {
                points.push({row: row, column: column});
            }
        }
        return points;
    }

    function reverseCopy(points) {
        var result = [];
        var index;
        for (index = points.length - 1; index >= 0; index -= 1) {
            result.push(points[index]);
        }
        return result;
    }

    function showDialog() {
        var dialog = new Window(
            "dialog",
            "SurfaceLab v1 — Chain Edges");
        dialog.orientation = "column";
        dialog.alignChildren = ["fill", "top"];

        var description = dialog.add(
            "statictext",
            undefined,
            "Issue both edges with Null Rig first, then link them here.",
            {multiline: true});
        description.preferredSize.width = 390;

        var headers = dialog.add("group");
        headers.add("statictext", undefined, "");
        var headerA = headers.add("statictext", undefined, "Surface A");
        headerA.preferredSize.width = 115;
        var headerB = headers.add("statictext", undefined, "Surface B");
        headerB.preferredSize.width = 115;

        var surfaceGroup = dialog.add("group");
        surfaceGroup.add("statictext", undefined, "Surface");
        var surfaceItems = ["1", "2", "3", "4", "5", "6", "7", "8"];
        var surfaceA = surfaceGroup.add(
            "dropdownlist",
            undefined,
            surfaceItems);
        var surfaceB = surfaceGroup.add(
            "dropdownlist",
            undefined,
            surfaceItems);
        surfaceA.selection = 0;
        surfaceB.selection = 1;

        var edgeGroup = dialog.add("group");
        edgeGroup.add("statictext", undefined, "Edge");
        var edgeItems = ["Left", "Right", "Top", "Bottom"];
        var edgeA = edgeGroup.add(
            "dropdownlist",
            undefined,
            edgeItems);
        var edgeB = edgeGroup.add(
            "dropdownlist",
            undefined,
            edgeItems);
        edgeA.selection = 1;
        edgeB.selection = 0;

        var reverse = dialog.add(
            "checkbox",
            undefined,
            "Reverse Surface B point order");
        reverse.value = false;

        var removeRedundant = dialog.add(
            "checkbox",
            undefined,
            "Remove redundant Surface B Null layers");
        removeRedundant.value = false;

        var quickUv = dialog.add(
            "checkbox",
            undefined,
            "Apply quick two-panel UV crop");
        quickUv.value = false;

        var buttons = dialog.add("group");
        buttons.alignment = "right";
        buttons.add("button", undefined, "Cancel", {name: "cancel"});
        buttons.add("button", undefined, "Chain", {name: "ok"});

        if (dialog.show() !== 1) {
            return null;
        }
        return {
            surfaceA: surfaceA.selection.index,
            surfaceB: surfaceB.selection.index,
            edgeA: edgeA.selection.index,
            edgeB: edgeB.selection.index,
            reverse: reverse.value,
            removeRedundant: removeRedundant.value,
            quickUv: quickUv.value
        };
    }

    function applyQuickUv(effect, options) {
        var complementary =
            (options.edgeA === 1 && options.edgeB === 0) ||
            (options.edgeA === 0 && options.edgeB === 1) ||
            (options.edgeA === 3 && options.edgeB === 2) ||
            (options.edgeA === 2 && options.edgeB === 3);
        if (!complementary) {
            throw new Error(
                "Quick UV requires complementary Left/Right or Top/Bottom edges.");
        }
        var sourceA = Number(effect.property(surfacePropertyIndex(
            options.surfaceA,
            SURFACE_SOURCE_OFFSET)).value);
        var sourceB = Number(effect.property(surfacePropertyIndex(
            options.surfaceB,
            SURFACE_SOURCE_OFFSET)).value);
        if (sourceA !== sourceB) {
            throw new Error(
                "Quick UV requires both surfaces to use the same Source Layer.");
        }
        var transformA = effect.property(surfacePropertyIndex(
            options.surfaceA,
            SURFACE_IMAGE_TRANSFORM_OFFSET));
        var transformB = effect.property(surfacePropertyIndex(
            options.surfaceB,
            SURFACE_IMAGE_TRANSFORM_OFFSET));
        var valueA = transformA.value;
        var valueB = transformB.value;
        var xA = 0;
        var yA = 0;
        var xB = 0;
        var yB = 0;
        if (options.edgeA === 1) {
            xA = 50;
            xB = -50;
        } else if (options.edgeA === 0) {
            xA = -50;
            xB = 50;
        } else if (options.edgeA === 3) {
            yA = 50;
            yB = -50;
        } else {
            yA = -50;
            yB = 50;
        }
        transformA.setValue([xA, yA, Number(valueA[2])]);
        transformB.setValue([xB, yB, Number(valueB[2])]);
        effect.property(surfacePropertyIndex(
            options.surfaceA,
            SURFACE_IMAGE_SCALE_OFFSET)).setValue(200);
        effect.property(surfacePropertyIndex(
            options.surfaceB,
            SURFACE_IMAGE_SCALE_OFFSET)).setValue(200);
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
    if (options.surfaceA === options.surfaceB) {
        alert(
            "SurfaceLab Chain Edges\n\nChoose two different surfaces.");
        return;
    }

    app.beginUndoGroup("Chain SurfaceLab Edges");
    try {
        var infoA = surfaceInfo(host.effect, options.surfaceA);
        var infoB = surfaceInfo(host.effect, options.surfaceB);
        var pointsA = edgePoints(infoA, options.edgeA);
        var pointsB = edgePoints(infoB, options.edgeB);
        if (options.reverse) {
            pointsB = reverseCopy(pointsB);
        }
        if (pointsA.length !== pointsB.length) {
            throw new Error(
                "The selected edges have " + pointsA.length +
                " and " + pointsB.length +
                " points. Match their edge divisions first.");
        }

        var hostId = String(host.layer.id);
        var pairs = [];
        var missing = [];
        var index;
        for (index = 0; index < pointsA.length; index += 1) {
            var commentA = pointComment(
                hostId,
                infoA,
                pointsA[index].row,
                pointsA[index].column);
            var commentB = pointComment(
                hostId,
                infoB,
                pointsB[index].row,
                pointsB[index].column);
            var layerA = findLayerWithMarker(comp, commentA);
            var layerB = findLayerWithMarker(comp, commentB);
            if (!layerA || !layerB) {
                missing.push(
                    "pair " + (index + 1) +
                    (!layerA ? " (A)" : "") +
                    (!layerB ? " (B)" : ""));
            } else {
                pairs.push({
                    layerA: layerA,
                    layerB: layerB,
                    commentA: commentA,
                    commentB: commentB
                });
            }
        }
        if (missing.length > 0) {
            throw new Error(
                "Issue both selected edges with Null Rig first.\nMissing: " +
                missing.join(", "));
        }

        var controllers = [];
        for (index = 0; index < pairs.length; index += 1) {
            var pair = pairs[index];
            if (pair.layerA !== pair.layerB) {
                addMarkerComment(
                    pair.layerA,
                    pair.commentB,
                    comp);
                removeMarkerComment(
                    pair.layerB,
                    pair.commentB);
                if (options.removeRedundant &&
                    pair.layerB.property("ADBE Marker").numKeys === 0) {
                    pair.layerB.remove();
                }
            }
            pair.layerA.name =
                "SL S" + (options.surfaceA + 1) +
                "-S" + (options.surfaceB + 1) +
                " Edge " + index;
            controllers.push(pair.layerA);
        }

        if (options.quickUv) {
            applyQuickUv(host.effect, options);
        }

        for (index = 1; index <= comp.numLayers; index += 1) {
            comp.layer(index).selected = false;
        }
        for (index = 0; index < controllers.length; index += 1) {
            controllers[index].selected = true;
        }
        alert(
            "SurfaceLab v1\n\nChained " + controllers.length +
            " shared edge Nulls." +
            (options.quickUv
                ? "\nApplied the uniform 200% two-panel UV crop."
                : "\nUV unchanged. For an exact uncropped span, use pre-sliced source precomps."));
    } catch (error) {
        alert("SurfaceLab Chain Edges\n\n" + error.toString());
    } finally {
        app.endUndoGroup();
    }
}());
