namespace RfaFormats.Zones;

/// <summary>
/// One object naming another: in one of its properties (<paramref name="Property"/>, the name's
/// hash), or by being attached to it (<paramref name="Property"/> null: <paramref name="From"/> is
/// attached to <paramref name="To"/>).
/// </summary>
public sealed record ZoneLink(ZoneObject From, uint? Property, ZoneObject To);

/// <summary>
/// The links between the objects of a zone and its mission layers, as the game stores them: the
/// handles of other objects inside properties. Surveyed over every zone and layer in the game (313
/// files): navpoints list the navpoints they lead to (obj_links, about 15,000 links), roads name
/// the pieces before and after them (path_road_data, NEXT), patrols their first navpoint
/// (patrol_start), logic handlers the triggers and objects they act on (logic_handler_buffer),
/// animated rigs the objects they move (anim_rig_children), and effects, physics constraints
/// (host_handle, child_handle) and lights sharing a shadow map theirs. So any 4-byte word of a
/// property that is another object's handle counts as a link, whatever the property. Left out:
/// uid (the object's number in its chunk file) and dynamic_links (numbers of destruction links,
/// not handles). An object attached to another (its parent) is a link too.
/// </summary>
public static class ZoneLinks
{
    static readonly uint Uid = NameHashes.PropertyHash("uid"), DynamicLinks = NameHashes.PropertyHash("dynamic_links");

    /// <summary>
    /// Every link among these files' objects. A handle is looked for in the naming object's own
    /// file first, then in the others in the order given (the zone itself first): the persistent
    /// zone and mission layers can hold copies with the same handle.
    /// </summary>
    public static List<ZoneLink> Find(IReadOnlyList<ZoneFile> files)
    {
        var anywhere = new Dictionary<uint, ZoneObject>();
        foreach (ZoneFile file in files)
            foreach (ZoneObject obj in file.Objects)
                anywhere.TryAdd(obj.Handle, obj);

        var links = new List<ZoneLink>();
        foreach (ZoneFile file in files)
        {
            var here = new Dictionary<uint, ZoneObject>();
            foreach (ZoneObject obj in file.Objects) here.TryAdd(obj.Handle, obj);
            ZoneObject? Named(uint handle) => here.TryGetValue(handle, out ZoneObject? obj) ? obj : anywhere.GetValueOrDefault(handle);

            foreach (ZoneObject obj in file.Objects)
            {
                if (obj.Parent != ZoneObject.None && Named(obj.Parent) is ZoneObject parent && parent != obj)
                    links.Add(new ZoneLink(obj, null, parent));
                foreach (ZoneProperty property in obj.Properties)
                {
                    if (property.NameHash == Uid || property.NameHash == DynamicLinks || property.Size < 4) continue;
                    byte[] value = property.Value;
                    var named = new HashSet<ZoneObject>();  // a property naming the same object twice is one link
                    for (int at = 0; at + 4 <= value.Length; at += 4)
                    {
                        uint word = BitConverter.ToUInt32(value, at);
                        if (word == obj.Handle || word == ZoneObject.None || word == 0) continue;
                        if (Named(word) is ZoneObject other && named.Add(other)) links.Add(new ZoneLink(obj, property.NameHash, other));
                    }
                }
            }
        }
        return links;
    }
}
