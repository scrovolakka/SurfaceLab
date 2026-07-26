/*
 * SurfaceLab v1 Null rig issuer
 *
 * Point controllers are identified exclusively by layer markers containing
 * the host layer ID, persistent surface ID, and lattice row/column. Names are
 * for readability only. Re-running the script reuses matching Nulls without
 * overwriting their transforms or animation; animation flows one way from
 * Nulls back into SurfaceLab. New Point Nulls use the surface tangent frame;
 * an optional oriented Surface Root supplies rigid whole-surface motion.
 */
(function surfaceLabCreateNullRig() {
    var EFFECT_MATCH_NAME = "XPK SurfaceLab";
    var SURFACE_COUNT = 8;
    var SURFACE_STRIDE = 26;
    var SURFACE_PARAMETERS_START = 11;
    var SCENE_POSITION = 2;
    var SCENE_ROTATION_X = 3;
    var SCENE_ROTATION_Y = 4;
    var SCENE_ROTATION_Z = 5;
    var SCENE_SCALE_X = 6;
    var SCENE_SCALE_Y = 7;
    var SCENE_SCALE_Z = 8;
    var SURFACE_POSITION_OFFSET = 2;
    var SURFACE_ROTATION_X_OFFSET = 3;
    var SURFACE_ROTATION_Y_OFFSET = 4;
    var SURFACE_ROTATION_Z_OFFSET = 5;
    var SURFACE_SCALE_X_OFFSET = 6;
    var SURFACE_SCALE_Y_OFFSET = 7;
    var SURFACE_SCALE_Z_OFFSET = 8;
    // AE effect property indices (the PF input parameter is omitted). Keep in
    // sync with SurfaceLab.h after any Surface parameter-stride change.
    var RIG_SURFACE = 226;
    var RIG_ROW = 227;
    var RIG_SURFACE_ID_0 = 228;
    var RIG_DIVISIONS_X = 232;
    var RIG_DIVISIONS_Y = 233;
    var RIG_POINTS_START = 234;

    function surfacePropertyIndex(surfaceIndex, offset) {
        return SURFACE_PARAMETERS_START +
            surfaceIndex * SURFACE_STRIDE + offset;
    }

    function findHost(comp) {
        var selected = comp.selectedLayers;
        var layerIndex;
        var effectIndex;
        for (layerIndex = 0; layerIndex < selected.length; layerIndex += 1) {
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
        for (layerIndex = 1; layerIndex <= comp.numLayers; layerIndex += 1) {
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

    function propertyValue(effect, index) {
        var property = effect.property(index);
        if (!property) {
            throw new Error(
                "SurfaceLab parameter " + index + " was not found.");
        }
        return property.value;
    }

    function scalar(effect, index) {
        var value = propertyValue(effect, index);
        return value instanceof Array ? Number(value[0]) : Number(value);
    }

    function parseLattice(effect, surfaceIndex) {
        var rigSurface = effect.property(RIG_SURFACE);
        var rigRow = effect.property(RIG_ROW);
        if (!rigSurface || !rigRow ||
            !effect.property(RIG_POINTS_START)) {
            throw new Error(
                "The loaded SurfaceLab build has no Null Rig Bridge. " +
                "Install the current plug-in build and restart After Effects.");
        }
        rigSurface.setValue(surfaceIndex + 1);
        rigRow.setValue(1);
        rigRow.setValue(0);
        var dx = parseInt(
            effect.property(RIG_DIVISIONS_X).value,
            10);
        var dy = parseInt(
            effect.property(RIG_DIVISIONS_Y).value,
            10);
        if (isNaN(dx) || isNaN(dy) ||
            dx < 1 || dx > 16 || dy < 1 || dy > 16) {
            throw new Error(
                "SurfaceLab did not publish valid lattice dimensions.");
        }
        var idChunks = [];
        var chunk;
        for (chunk = 0; chunk < 4; chunk += 1) {
            idChunks.push(parseInt(
                effect.property(RIG_SURFACE_ID_0 + chunk).value,
                10));
        }
        var points = [];
        var row;
        var column;
        for (row = 0; row <= dy; row += 1) {
            rigRow.setValue(row === 16 ? 15 : row + 1);
            rigRow.setValue(row);
            for (column = 0; column <= dx; column += 1) {
                var point = effect.property(
                    RIG_POINTS_START + column).value;
                points.push([
                    Number(point[0]),
                    Number(point[1]),
                    Number(point[2])
                ]);
            }
        }
        return {
            surfaceIdChunks: idChunks,
            divisionsX: dx,
            divisionsY: dy,
            points: points
        };
    }

    function rotate(point, rx, ry, rz) {
        var sx = Math.sin(rx);
        var cx = Math.cos(rx);
        var sy = Math.sin(ry);
        var cy = Math.cos(ry);
        var sz = Math.sin(rz);
        var cz = Math.cos(rz);
        var yx = point[1] * cx - point[2] * sx;
        var zx = point[1] * sx + point[2] * cx;
        var xz = point[0] * cy + zx * sy;
        var zy = -point[0] * sy + zx * cy;
        return [
            xz * cz - yx * sz,
            xz * sz + yx * cz,
            zy
        ];
    }

    function degrees(value) {
        return Number(value) * Math.PI / 180.0;
    }

    function buildWorldPoints(comp, effect, surfaceIndex, lattice) {
        var surfacePosition = propertyValue(
            effect,
            surfacePropertyIndex(
                surfaceIndex,
                SURFACE_POSITION_OFFSET));
        var surfaceRotation = [
            degrees(scalar(
                effect,
                surfacePropertyIndex(
                    surfaceIndex,
                    SURFACE_ROTATION_X_OFFSET))),
            degrees(scalar(
                effect,
                surfacePropertyIndex(
                    surfaceIndex,
                    SURFACE_ROTATION_Y_OFFSET))),
            degrees(scalar(
                effect,
                surfacePropertyIndex(
                    surfaceIndex,
                    SURFACE_ROTATION_Z_OFFSET)))
        ];
        var surfaceScale = [
            scalar(
                effect,
                surfacePropertyIndex(
                    surfaceIndex,
                    SURFACE_SCALE_X_OFFSET)) / 100.0,
            scalar(
                effect,
                surfacePropertyIndex(
                    surfaceIndex,
                    SURFACE_SCALE_Y_OFFSET)) / 100.0,
            scalar(
                effect,
                surfacePropertyIndex(
                    surfaceIndex,
                    SURFACE_SCALE_Z_OFFSET)) / 100.0
        ];
        var scenePosition = propertyValue(effect, SCENE_POSITION);
        var sceneRotation = [
            degrees(scalar(effect, SCENE_ROTATION_X)),
            degrees(scalar(effect, SCENE_ROTATION_Y)),
            degrees(scalar(effect, SCENE_ROTATION_Z))
        ];
        var sceneScale = [
            scalar(effect, SCENE_SCALE_X) / 100.0,
            scalar(effect, SCENE_SCALE_Y) / 100.0,
            scalar(effect, SCENE_SCALE_Z) / 100.0
        ];
        var minimum = [
            lattice.points[0][0],
            lattice.points[0][1],
            lattice.points[0][2]
        ];
        var maximum = [minimum[0], minimum[1], minimum[2]];
        var index;
        var axis;
        for (index = 1; index < lattice.points.length; index += 1) {
            for (axis = 0; axis < 3; axis += 1) {
                minimum[axis] = Math.min(
                    minimum[axis],
                    lattice.points[index][axis]);
                maximum[axis] = Math.max(
                    maximum[axis],
                    lattice.points[index][axis]);
            }
        }
        var latticeCenter = [
            (minimum[0] + maximum[0]) * 0.5,
            (minimum[1] + maximum[1]) * 0.5,
            (minimum[2] + maximum[2]) * 0.5
        ];
        var scenePivot = [comp.width * 0.5, comp.height * 0.5, 0];
        var world = [];
        for (index = 0; index < lattice.points.length; index += 1) {
            var cage = [
                lattice.points[index][0] - latticeCenter[0] +
                    surfacePosition[0],
                lattice.points[index][1] - latticeCenter[1] +
                    surfacePosition[1],
                lattice.points[index][2] - latticeCenter[2] +
                    surfacePosition[2]
            ];
            var scaled = [
                (cage[0] - surfacePosition[0]) * surfaceScale[0],
                (cage[1] - surfacePosition[1]) * surfaceScale[1],
                (cage[2] - surfacePosition[2]) * surfaceScale[2]
            ];
            var surfaceRotated = rotate(
                scaled,
                surfaceRotation[0],
                surfaceRotation[1],
                surfaceRotation[2]);
            var surfaceWorld = [
                surfacePosition[0] + surfaceRotated[0],
                surfacePosition[1] + surfaceRotated[1],
                surfacePosition[2] + surfaceRotated[2]
            ];
            var sceneRelative = [
                (surfaceWorld[0] - scenePivot[0]) * sceneScale[0],
                (surfaceWorld[1] - scenePivot[1]) * sceneScale[1],
                (surfaceWorld[2] - scenePivot[2]) * sceneScale[2]
            ];
            var sceneRotated = rotate(
                sceneRelative,
                sceneRotation[0],
                sceneRotation[1],
                sceneRotation[2]);
            world.push([
                scenePosition[0] + sceneRotated[0],
                scenePosition[1] + sceneRotated[1],
                scenePosition[2] + sceneRotated[2]
            ]);
        }
        return world;
    }

    function subtract(first, second) {
        return [
            first[0] - second[0],
            first[1] - second[1],
            first[2] - second[2]
        ];
    }

    function dot(first, second) {
        return first[0] * second[0] +
            first[1] * second[1] +
            first[2] * second[2];
    }

    function cross(first, second) {
        return [
            first[1] * second[2] - first[2] * second[1],
            first[2] * second[0] - first[0] * second[2],
            first[0] * second[1] - first[1] * second[0]
        ];
    }

    function normalize(vector, fallback) {
        var length = Math.sqrt(dot(vector, vector));
        if (!isFinite(length) || length < 0.000001) {
            return [fallback[0], fallback[1], fallback[2]];
        }
        return [
            vector[0] / length,
            vector[1] / length,
            vector[2] / length
        ];
    }

    function buildFrame(xDirection, yDirection) {
        var xAxis = normalize(xDirection, [1, 0, 0]);
        var yProjected = [
            yDirection[0] - xAxis[0] * dot(yDirection, xAxis),
            yDirection[1] - xAxis[1] * dot(yDirection, xAxis),
            yDirection[2] - xAxis[2] * dot(yDirection, xAxis)
        ];
        var yAxis = normalize(yProjected, [0, 1, 0]);
        var zAxis = normalize(cross(xAxis, yAxis), [0, 0, 1]);
        yAxis = normalize(cross(zAxis, xAxis), [0, 1, 0]);
        return {
            x: xAxis,
            y: yAxis,
            z: zAxis
        };
    }

    function pointFrame(points, row, column, dx, dy) {
        var left = row * (dx + 1) + Math.max(0, column - 1);
        var right = row * (dx + 1) + Math.min(dx, column + 1);
        var top = Math.max(0, row - 1) * (dx + 1) + column;
        var bottom = Math.min(dy, row + 1) * (dx + 1) + column;
        return buildFrame(
            subtract(points[right], points[left]),
            subtract(points[bottom], points[top]));
    }

    function buildPointFrames(points, dx, dy) {
        var frames = [];
        var row;
        var column;
        for (row = 0; row <= dy; row += 1) {
            for (column = 0; column <= dx; column += 1) {
                frames.push(pointFrame(
                    points,
                    row,
                    column,
                    dx,
                    dy));
            }
        }
        return frames;
    }

    function buildSurfaceFrame(points, dx, dy) {
        var left = [0, 0, 0];
        var right = [0, 0, 0];
        var top = [0, 0, 0];
        var bottom = [0, 0, 0];
        var row;
        var column;
        var axis;
        for (row = 0; row <= dy; row += 1) {
            for (axis = 0; axis < 3; axis += 1) {
                left[axis] += points[row * (dx + 1)][axis];
                right[axis] += points[row * (dx + 1) + dx][axis];
            }
        }
        for (column = 0; column <= dx; column += 1) {
            for (axis = 0; axis < 3; axis += 1) {
                top[axis] += points[column][axis];
                bottom[axis] +=
                    points[dy * (dx + 1) + column][axis];
            }
        }
        return buildFrame(
            subtract(right, left),
            subtract(bottom, top));
    }

    function frameOrientation(frame) {
        var radiansToDegrees = 180.0 / Math.PI;
        var sineY = Math.max(-1, Math.min(1, -frame.x[2]));
        var rotationY = Math.asin(sineY);
        var cosineY = Math.cos(rotationY);
        var rotationX;
        var rotationZ;
        if (Math.abs(cosineY) > 0.000001) {
            rotationX = Math.atan2(frame.y[2], frame.z[2]);
            rotationZ = Math.atan2(frame.x[1], frame.x[0]);
        } else {
            rotationX = 0;
            rotationZ = Math.atan2(-frame.y[0], frame.y[1]);
        }
        return [
            rotationX * radiansToDegrees,
            rotationY * radiansToDegrees,
            rotationZ * radiansToDegrees
        ];
    }

    function markerComment(layer) {
        var markers = layer.property("ADBE Marker");
        if (!markers || markers.numKeys < 1) {
            return "";
        }
        var key;
        for (key = 1; key <= markers.numKeys; key += 1) {
            var comment = markers.keyValue(key).comment;
            if (comment.indexOf("SurfaceLabV1|") === 0) {
                return comment;
            }
        }
        return "";
    }

    function findMarkedLayer(comp, identityComment) {
        var index;
        for (index = 1; index <= comp.numLayers; index += 1) {
            var comment = markerComment(comp.layer(index));
            if (comment === identityComment ||
                comment.indexOf(identityComment + "|") === 0) {
                return comp.layer(index);
            }
        }
        return null;
    }

    function findMarkedLayers(comp, identityPrefix) {
        var result = [];
        var index;
        for (index = 1; index <= comp.numLayers; index += 1) {
            var layer = comp.layer(index);
            if (markerComment(layer).indexOf(identityPrefix) === 0) {
                result.push(layer);
            }
        }
        return result;
    }

    function setMarker(layer, comment) {
        var marker = new MarkerValue(comment);
        layer.property("ADBE Marker").setValueAtTime(0, marker);
    }

    function prepareNull(layer) {
        layer.threeDLayer = true;
        layer.property("ADBE Transform Group")
            .property("ADBE Anchor Point")
            .setValue([0, 0, 0]);
    }

    function encodedRootMarker(identityComment, bindWorld, bindFrame) {
        return identityComment +
            "|rootv=4" +
            "|bindx=" + Math.round(bindWorld[0] * 1000.0) +
            "|bindy=" + Math.round(bindWorld[1] * 1000.0) +
            "|bindz=" + Math.round(bindWorld[2] * 1000.0) +
            "|bxx=" + Math.round(bindFrame.x[0] * 1000000.0) +
            "|bxy=" + Math.round(bindFrame.x[1] * 1000000.0) +
            "|bxz=" + Math.round(bindFrame.x[2] * 1000000.0) +
            "|byx=" + Math.round(bindFrame.y[0] * 1000000.0) +
            "|byy=" + Math.round(bindFrame.y[1] * 1000000.0) +
            "|byz=" + Math.round(bindFrame.y[2] * 1000000.0) +
            "|bzx=" + Math.round(bindFrame.z[0] * 1000000.0) +
            "|bzy=" + Math.round(bindFrame.z[1] * 1000000.0) +
            "|bzz=" + Math.round(bindFrame.z[2] * 1000000.0);
    }

    function worldBoundsCenter(points) {
        var minimum = [
            points[0][0],
            points[0][1],
            points[0][2]
        ];
        var maximum = [minimum[0], minimum[1], minimum[2]];
        var index;
        var axis;
        for (index = 1; index < points.length; index += 1) {
            for (axis = 0; axis < 3; axis += 1) {
                minimum[axis] = Math.min(
                    minimum[axis],
                    points[index][axis]);
                maximum[axis] = Math.max(
                    maximum[axis],
                    points[index][axis]);
            }
        }
        return [
            (minimum[0] + maximum[0]) * 0.5,
            (minimum[1] + maximum[1]) * 0.5,
            (minimum[2] + maximum[2]) * 0.5
        ];
    }

    function resetRootTransform(root, bindWorld, bindFrame) {
        if (root.parent) {
            root.parent = null;
        }
        var transform = root.property("ADBE Transform Group");
        transform.property("ADBE Position").setValue(bindWorld);
        transform.property("ADBE Orientation")
            .setValue(frameOrientation(bindFrame));
        transform.property("ADBE Rotate X").setValue(0);
        transform.property("ADBE Rotate Y").setValue(0);
        transform.property("ADBE Rotate Z").setValue(0);
        transform.property("ADBE Scale").setValue([100, 100, 100]);
    }

    function sampleLayerWorldTransform(layer) {
        var effects = layer.property("ADBE Effect Parade");
        var probe = effects.addProperty("ADBE Point3D Control");
        if (!probe) {
            throw new Error(
                "After Effects could not sample the Surface Root transform.");
        }
        var value = probe.property(1);
        function sample(expression) {
            value.expression = expression;
            var result = value.value;
            return [
                Number(result[0]),
                Number(result[1]),
                Number(result[2])
            ];
        }
        var origin;
        var xAxis;
        var yAxis;
        var zAxis;
        try {
            origin = sample("toWorld([0, 0, 0])");
            xAxis = sample("toWorldVec([1, 0, 0])");
            yAxis = sample("toWorldVec([0, 1, 0])");
            zAxis = sample("toWorldVec([0, 0, 1])");
        } finally {
            probe.remove();
        }
        return {
            world: origin,
            frame: {
                x: xAxis,
                y: yAxis,
                z: zAxis
            }
        };
    }

    function markerNumber(comment, key, divisor) {
        var token = "|" + key + "=";
        var start = comment.indexOf(token);
        if (start < 0) {
            throw new Error(
                "Surface Root marker is missing " + key + ".");
        }
        start += token.length;
        var end = comment.indexOf("|", start);
        if (end < 0) {
            end = comment.length;
        }
        var value = Number(comment.substring(start, end));
        if (!isFinite(value)) {
            throw new Error(
                "Surface Root marker has an invalid " + key + ".");
        }
        return value / divisor;
    }

    function rootBindTransform(root) {
        var comment = markerComment(root);
        if (comment.indexOf("|rootv=4|") < 0) {
            throw new Error(
                "Surface Root must be upgraded before issuing Point Nulls.");
        }
        return {
            world: [
                markerNumber(comment, "bindx", 1000.0),
                markerNumber(comment, "bindy", 1000.0),
                markerNumber(comment, "bindz", 1000.0)
            ],
            frame: {
                x: [
                    markerNumber(comment, "bxx", 1000000.0),
                    markerNumber(comment, "bxy", 1000000.0),
                    markerNumber(comment, "bxz", 1000000.0)
                ],
                y: [
                    markerNumber(comment, "byx", 1000000.0),
                    markerNumber(comment, "byy", 1000000.0),
                    markerNumber(comment, "byz", 1000000.0)
                ],
                z: [
                    markerNumber(comment, "bzx", 1000000.0),
                    markerNumber(comment, "bzy", 1000000.0),
                    markerNumber(comment, "bzz", 1000000.0)
                ]
            }
        };
    }

    function coordinatesInFrame(vector, frame) {
        var yCrossZ = cross(frame.y, frame.z);
        var determinant = dot(frame.x, yCrossZ);
        if (!isFinite(determinant) ||
            Math.abs(determinant) < 0.000000001) {
            throw new Error(
                "Surface Root bind frame cannot be inverted.");
        }
        return [
            dot(vector, yCrossZ) / determinant,
            dot(frame.x, cross(vector, frame.z)) / determinant,
            dot(frame.x, cross(frame.y, vector)) / determinant
        ];
    }

    function pointFromFrame(coordinates, transform) {
        return [
            transform.world[0] +
                transform.frame.x[0] * coordinates[0] +
                transform.frame.y[0] * coordinates[1] +
                transform.frame.z[0] * coordinates[2],
            transform.world[1] +
                transform.frame.x[1] * coordinates[0] +
                transform.frame.y[1] * coordinates[1] +
                transform.frame.z[1] * coordinates[2],
            transform.world[2] +
                transform.frame.x[2] * coordinates[0] +
                transform.frame.y[2] * coordinates[1] +
                transform.frame.z[2] * coordinates[2]
        ];
    }

    function mapPointBetweenRootFrames(point, bind, current) {
        return pointFromFrame(
            coordinatesInFrame(
                subtract(point, bind.world),
                bind.frame),
            current);
    }

    function mapPointsThroughRoot(points, root) {
        var bind = rootBindTransform(root);
        var current = sampleLayerWorldTransform(root);
        var mapped = [];
        var index;
        for (index = 0; index < points.length; index += 1) {
            mapped.push(mapPointBetweenRootFrames(
                points[index],
                bind,
                current));
        }
        return mapped;
    }

    function getOrCreateRoot(
        comp,
        identityComment,
        name,
        parent,
        bindWorld,
        bindFrame) {
        var root = findMarkedLayer(comp, identityComment);
        var needsUpgrade = true;
        if (!root) {
            root = comp.layers.addNull(comp.duration);
            root.name = name;
            prepareNull(root);
        } else {
            needsUpgrade =
                markerComment(root).indexOf("|rootv=4|") < 0;
        }
        if (needsUpgrade) {
            prepareNull(root);
            resetRootTransform(root, bindWorld, bindFrame);
            var sampledBind = sampleLayerWorldTransform(root);
            setMarker(
                root,
                encodedRootMarker(
                    identityComment,
                    sampledBind.world,
                    sampledBind.frame));
        }
        if (parent && root.parent !== parent) {
            root.parent = parent;
        } else if (!parent && root.parent) {
            root.parent = null;
        }
        return root;
    }

    function selectPointIndices(scope, row, column, dx, dy) {
        var indices = [];
        var r;
        var c;
        for (r = 0; r <= dy; r += 1) {
            for (c = 0; c <= dx; c += 1) {
                var include =
                    scope === 0 ||
                    (scope === 1 &&
                        (r === 0 || r === dy || c === 0 || c === dx)) ||
                    (scope === 2 && r === row) ||
                    (scope === 3 && c === column) ||
                    (scope === 4 && r === row && c === column);
                if (include) {
                    indices.push({
                        row: r,
                        column: c,
                        point: r * (dx + 1) + c
                    });
                }
            }
        }
        return indices;
    }

    function showDialog() {
        var dialog = new Window(
            "dialog",
            "SurfaceLab v1 — Null Rig");
        dialog.orientation = "column";
        dialog.alignChildren = ["fill", "top"];

        var surfaceGroup = dialog.add("group");
        surfaceGroup.add("statictext", undefined, "Surface");
        var surface = surfaceGroup.add(
            "dropdownlist",
            undefined,
            ["1", "2", "3", "4", "5", "6", "7", "8"]);
        surface.selection = 0;

        var scopeGroup = dialog.add("group");
        scopeGroup.add("statictext", undefined, "Range");
        var scope = scopeGroup.add(
            "dropdownlist",
            undefined,
            ["All", "Perimeter", "Row", "Column", "Point"]);
        scope.selection = 1;

        var coordinateGroup = dialog.add("group");
        coordinateGroup.add("statictext", undefined, "Row");
        var row = coordinateGroup.add("edittext", undefined, "0");
        row.characters = 4;
        coordinateGroup.add("statictext", undefined, "Column");
        var column = coordinateGroup.add("edittext", undefined, "0");
        column.characters = 4;

        var roots = dialog.add(
            "checkbox",
            undefined,
            "Create Surface Root");
        roots.value = true;

        var buttons = dialog.add("group");
        buttons.alignment = "right";
        buttons.add("button", undefined, "Cancel", {name: "cancel"});
        buttons.add("button", undefined, "Issue", {name: "ok"});

        if (dialog.show() !== 1) {
            return null;
        }
        return {
            surface: surface.selection.index,
            scope: scope.selection.index,
            row: parseInt(row.text, 10),
            column: parseInt(column.text, 10),
            roots: roots.value
        };
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

    app.beginUndoGroup("Issue SurfaceLab Null Rig");
    try {
        var lattice = parseLattice(host.effect, options.surface);
        if (isNaN(options.row)) {
            options.row = 0;
        }
        if (isNaN(options.column)) {
            options.column = 0;
        }
        options.row = Math.max(
            0,
            Math.min(lattice.divisionsY, options.row));
        options.column = Math.max(
            0,
            Math.min(lattice.divisionsX, options.column));
        var selectedPoints = selectPointIndices(
            options.scope,
            options.row,
            options.column,
            lattice.divisionsX,
            lattice.divisionsY);
        if (selectedPoints.length > 128 &&
            !confirm(
                "This will issue " + selectedPoints.length +
                " 3D Nulls.\n\nContinue?")) {
            return;
        }
        var worldPoints = buildWorldPoints(
            comp,
            host.effect,
            options.surface,
            lattice);
        var pointFrames = buildPointFrames(
            worldPoints,
            lattice.divisionsX,
            lattice.divisionsY);
        var surfaceFrame = buildSurfaceFrame(
            worldPoints,
            lattice.divisionsX,
            lattice.divisionsY);
        var hostId = String(host.layer.id);
        var surfaceRoot = null;
        var detachedPointNulls = [];
        if (options.roots) {
            var surfaceCenter = worldBoundsCenter(worldPoints);
            var surfaceIdentity =
                "|id0=" + lattice.surfaceIdChunks[0] +
                "|id1=" + lattice.surfaceIdChunks[1] +
                "|id2=" + lattice.surfaceIdChunks[2] +
                "|id3=" + lattice.surfaceIdChunks[3];
            var surfaceRootIdentity =
                "SurfaceLabV1|surface-root|host=" + hostId +
                surfaceIdentity;
            var existingSurfaceRoot = findMarkedLayer(
                comp,
                surfaceRootIdentity);
            var rootsNeedUpgrade =
                !existingSurfaceRoot ||
                markerComment(existingSurfaceRoot)
                    .indexOf("|rootv=4|") < 0;
            if (rootsNeedUpgrade) {
                detachedPointNulls = findMarkedLayers(
                    comp,
                    "SurfaceLabV1|point|host=" + hostId +
                        surfaceIdentity + "|");
                var detachedIndex;
                for (detachedIndex = 0;
                     detachedIndex < detachedPointNulls.length;
                     detachedIndex += 1) {
                    if (detachedPointNulls[detachedIndex].parent) {
                        detachedPointNulls[detachedIndex].parent = null;
                    }
                }
            }
            surfaceRoot = getOrCreateRoot(
                comp,
                surfaceRootIdentity,
                "SL S" + (options.surface + 1) + " Root",
                null,
                surfaceCenter,
                surfaceFrame);
            for (detachedIndex = 0;
                 detachedIndex < detachedPointNulls.length;
                 detachedIndex += 1) {
                detachedPointNulls[detachedIndex].parent = surfaceRoot;
            }
            // buildWorldPoints describes the unrooted surface. If an existing
            // Surface Root has already moved, issue new Point Nulls on the
            // current rooted surface, not at its stale bind pose. Rebuilding
            // tangent frames from the mapped points also gives every new Null
            // the direction currently visible in the Comp panel.
            worldPoints = mapPointsThroughRoot(
                worldPoints,
                surfaceRoot);
            pointFrames = buildPointFrames(
                worldPoints,
                lattice.divisionsX,
                lattice.divisionsY);
        }
        var issued = [];
        var index;
        for (index = 0; index < selectedPoints.length; index += 1) {
            var selectedPoint = selectedPoints[index];
            var comment =
                "SurfaceLabV1|point|host=" + hostId +
                "|id0=" + lattice.surfaceIdChunks[0] +
                "|id1=" + lattice.surfaceIdChunks[1] +
                "|id2=" + lattice.surfaceIdChunks[2] +
                "|id3=" + lattice.surfaceIdChunks[3] +
                "|row=" + selectedPoint.row +
                "|col=" + selectedPoint.column;
            var pointNull = findMarkedLayer(comp, comment);
            var pointNullIsNew = !pointNull;
            if (!pointNull) {
                pointNull = comp.layers.addNull(comp.duration);
                pointNull.name =
                    "SL S" + (options.surface + 1) +
                    " R" + selectedPoint.row +
                    " C" + selectedPoint.column;
                prepareNull(pointNull);
                setMarker(pointNull, comment);
            }
            if (pointNullIsNew) {
                if (pointNull.parent) {
                    pointNull.parent = null;
                }
                pointNull.property("ADBE Transform Group")
                    .property("ADBE Position")
                    .setValue(worldPoints[selectedPoint.point]);
                pointNull.property("ADBE Transform Group")
                    .property("ADBE Orientation")
                    .setValue(frameOrientation(
                        pointFrames[selectedPoint.point]));
            }
            if (surfaceRoot && pointNull.parent !== surfaceRoot) {
                pointNull.parent = surfaceRoot;
            }
            issued.push(pointNull);
        }
        for (index = 1; index <= comp.numLayers; index += 1) {
            comp.layer(index).selected = false;
        }
        for (index = 0; index < issued.length; index += 1) {
            issued[index].selected = true;
        }
        alert(
            "SurfaceLab v1\n\nIssued " + issued.length +
            " marker-linked point Null" +
            (issued.length === 1 ? "." : "s."));
    } catch (error) {
        alert("SurfaceLab v1 Null Rig\n\n" + error.toString());
    } finally {
        app.endUndoGroup();
    }
}());
