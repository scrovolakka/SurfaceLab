/*
 * SurfaceLab v1 setup
 *
 * Creates the 2D composition-sized host window and applies SurfaceLab.
 * SurfaceLab follows AE's active camera or AE's implicit default camera; it
 * never creates a camera layer of its own.
 */
(function surfaceLabSetup() {
    app.beginUndoGroup("Set Up SurfaceLab v1");
    try {
        var comp = app.project && app.project.activeItem;
        if (!(comp instanceof CompItem)) {
            throw new Error("Open or select a composition first.");
        }

        var host = comp.layers.addSolid(
            [0, 0, 0],
            "SurfaceLab Host",
            comp.width,
            comp.height,
            comp.pixelAspect,
            comp.duration);
        host.threeDLayer = false;
        host.moveToEnd();

        var effects = host.property("ADBE Effect Parade");
        var effect = effects.addProperty("XPK SurfaceLab");
        if (!effect) {
            throw new Error(
                "SurfaceLab is not installed or After Effects could not load it.");
        }

        host.selected = true;
    } catch (error) {
        alert("SurfaceLab v1 setup\n\n" + error.toString());
    } finally {
        app.endUndoGroup();
    }
}());
