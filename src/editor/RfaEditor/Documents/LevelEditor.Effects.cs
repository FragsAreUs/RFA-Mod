using RfaFormats.Zones;

namespace RfaEditor;

// Effects follow objects into any level: an object added to a zone (a chunk from another level or
// the Guerrilla world, a copy, a new one) shows what its level object (Props) names when it breaks,
// is hit or burns, and dies in its explosion, only where the level loads those effects' files.
// Armageddon's own levels load only their own props' effects, so on saving the effect files of
// the added objects' level objects (and of their explosions) that the level hasn't are brought
// into its containers, as their models are. Effects loaded at start-up (Preload) and their
// textures (effects.vpp_pc) are everywhere already.
public partial class LevelEditor
{
    /// <summary>
    /// Brings the effect files the objects added to this zone (those not in the game's own copy)
    /// need and the level hasn't into every level container holding the zone. Returns what was
    /// done, for the log ("" if nothing).
    /// </summary>
    string BringEffects()
    {
        if (!context.Assets.IndexReady) return "";
        var levelObjects = zone.Objects.Where(o => !IsGeometry(o) && AddedOrMoved(o) && placedAtOpen != null && !placedAtOpen.ContainsKey(o.Handle))
                                       .Select(o => o.Property("Props")?.Text).OfType<string>().Where(p => p.Length > 0 && !p.Equals("NONE", StringComparison.OrdinalIgnoreCase))
                                       .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        if (levelObjects.Count == 0) return "";
        var targets = ChunkContainers();
        if (targets.Count == 0) return "";
        // The archives this level loads its files from: those of the zone's copies, of its stitch
        // list and of its containers, and their world's (rfg_l0, rfg_l1, rfg_precache).
        var levelArchives = Copies().Select(c => c.Archive).Concat(targets.Select(t => t.Archive))
                                    .Concat(stitchListName == null ? [] : context.Assets.PlacesOf(stitchListName).Select(p => p.Archive))
                                    .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        bool InLevel(string inArchive) => levelArchives.Any(a => ContainerEdits.SameLevel(a, inArchive));
        // A file the level has: loose in one of its archives, or in one of the containers it loads.
        bool Present(string file) => context.Assets.PlacesOf(file).Any(p => InLevel(p.Archive) &&
                                         (p.Container == null || targets.Any(t => t.Archive.Equals(p.Archive, StringComparison.OrdinalIgnoreCase) &&
                                                                                  t.Container.Equals(p.Container, StringComparison.OrdinalIgnoreCase))));
        if (context.Assets.EffectPackageFor(levelObjects, Present, out List<string> effects) is not { } package) return "";
        var said = new List<string>();
        foreach (var to in targets)
        {
            var have = Workspace.Container(to.Archive, to.Container);
            var files = package.Files.Where(f => have.Find(f.Name) == null).ToList();
            var primitives = package.Primitives.Where(p => have.Find(p.Name) == null).ToList();
            if (files.Count == 0) continue;
            said.Add($"into {to.Container}: " + ContainerEdits.AddFiles(Workspace, to.Archive, to.Container, files, primitives, []).TrimEnd('.'));
        }
        return said.Count == 0 ? "" :
            $" Effects of the added objects (when they break, are hit, burn or blow up) the level hadn't: {string.Join(", ", effects)}; " + string.Join("; ", said) + ".";
    }

    /// <summary>
    /// Brings the animations of the zone's animated rigs (monoliths, tentacles, ...: object_anim_rig's rig_name) that the
    /// level hasn't into every level container holding the zone, as <see cref="BringEffects"/> brings effects: a rig
    /// copied from an Infestation level into a Ruin one would otherwise have none to play. Returns what was done, for the
    /// log ("" if nothing).
    /// </summary>
    string BringRigAnimations()
    {
        if (!context.Assets.IndexReady) return "";
        var rigs = zone.Objects.Where(o => names.Class(o.ClassHash).Equals("object_anim_rig", StringComparison.OrdinalIgnoreCase))
                               .Select(o => o.Property("rig_name")?.Text).OfType<string>().Where(r => r.Trim().Length > 0).Select(r => r.Trim())
                               .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        if (rigs.Count == 0) return "";
        var targets = ChunkContainers();
        if (targets.Count == 0) return "";
        var levelArchives = Copies().Select(c => c.Archive).Concat(targets.Select(t => t.Archive))
                                    .Concat(stitchListName == null ? [] : context.Assets.PlacesOf(stitchListName).Select(p => p.Archive))
                                    .Distinct(StringComparer.OrdinalIgnoreCase).ToList();
        bool InLevel(string inArchive) => levelArchives.Any(a => ContainerEdits.SameLevel(a, inArchive));
        bool Present(string file) => context.Assets.PlacesOf(file).Any(p => InLevel(p.Archive) &&
                                         (p.Container == null || targets.Any(t => t.Archive.Equals(p.Archive, StringComparison.OrdinalIgnoreCase) &&
                                                                                  t.Container.Equals(p.Container, StringComparison.OrdinalIgnoreCase))));
        if (context.Assets.RigAnimationPackageFor(rigs, Present, out List<string> missing) is not { } package) return "";
        var said = new List<string>();
        foreach (var to in targets)
        {
            var have = Workspace.Container(to.Archive, to.Container);
            var files = package.Files.Where(f => have.Find(f.Name) == null).ToList();
            var primitives = package.Primitives.Where(p => have.Find(p.Name) == null).ToList();
            if (files.Count == 0) continue;
            said.Add($"into {to.Container}: " + ContainerEdits.AddFiles(Workspace, to.Archive, to.Container, files, primitives, []).TrimEnd('.'));
        }
        return said.Count == 0 ? "" :
            $" Animations of the animated rigs ({string.Join(", ", missing)}) the level hadn't; " + string.Join("; ", said) + ".";
    }
}
