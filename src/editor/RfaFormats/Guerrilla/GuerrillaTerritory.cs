using System.Text;
using System.Text.RegularExpressions;

namespace RfaFormats.Guerrilla;

/// <summary>
/// Red Faction: Guerrilla's districts for a world ported from it, as a territory in Armageddon's
/// territories.xtbl. Both games read the table at start-up: a territory (looked up by the world's
/// name) lists its districts, each with the zones it covers (Grid_index, "CCRR": column, row). For
/// each district the game makes a district object, and gives each zone its district; its time of
/// day loads &lt;world&gt;_&lt;district&gt;.dtodx for each (spaces as _; nine sets at most, the default
/// district's among them) and blends the sets of the zone you are in and its neighbours by where you
/// stand in it, as Guerrilla does with its own (see <see cref="GuerrillaTimeOfDay"/>).
///
/// The territory is Guerrilla's terr01 under the world's name: its eight districts (Tutorial, Parker,
/// Dust, Badlands, Oasis, Manufacturing, Eos, End Game) with their zones, colours and effect tints, as
/// Guerrilla's table has them, less what Armageddon's districts don't have (thumbnails, loading
/// pictures) and Guerrilla's district flags (its faction war's). Its streaming settings (which zone
/// edges keep the next zone out) are left empty, as the world has had so far. The rest of the table is
/// Armageddon's own, as it is.
///
/// The districts are named by two letters (Tu, Pa, Du, Ba, Oa, Ma, Eo, EG): Armageddon keeps their
/// names in a pool of 32 bytes ("District Name Pool"; its own territories have no districts), each
/// name taking its length and 2 or 3 more, so Guerrilla's names stopped the game at start-up
/// ("Length of string pool exceeded"); eight names of two letters take 4 bytes each, the 32 exactly.
/// The names only pick the time-of-day files (rfg_eo.dtodx, ...); nothing shows them.
/// </summary>
public static class GuerrillaTerritory
{
    /// <summary>Armageddon's room for district names, in bytes, and what a name of n letters takes in it.</summary>
    const int NamePool = 32;
    static int NameRoom(string name) => name.Length + (name.Length % 2 == 1 ? 3 : 2);

    /// <summary>
    /// Armageddon's territories.xtbl (from its table.vpp_pc) with Guerrilla's terr01 (from its
    /// misc.vpp_pc) added as the territory <paramref name="world"/>, and each district's name there
    /// (two letters) and in Guerrilla.
    /// </summary>
    public static (byte[] Table, IReadOnlyList<(string Name, string Guerrilla)> Districts) Build(Packfile armageddonTable, Packfile guerrillaMisc, string world)
    {
        string Text(Packfile archive, string name) =>
            Encoding.Latin1.GetString(archive.Read(archive.Find(name) ?? throw new InvalidDataException($"no {name} in {archive.Name}")));
        string ours = Text(armageddonTable, "territories.xtbl"), theirs = Text(guerrillaMisc, "territories.xtbl");
        if (Regex.IsMatch(ours, $@"<Territory>\s*<Name>{Regex.Escape(world)}</Name>", RegexOptions.IgnoreCase))
            throw new InvalidDataException($"Armageddon's territories.xtbl already has a territory {world}");

        string table = Regex.Match(theirs, @"<Table>(.*?)</Table>", RegexOptions.Singleline) is { Success: true } t ? t.Groups[1].Value
            : throw new InvalidDataException("Guerrilla's territories.xtbl has no table");
        string terr01 = Regex.Match(table, @"<Territory>\s*<Name>terr01</Name>.*?</Territory>", RegexOptions.Singleline) is { Success: true } m ? m.Value
            : throw new InvalidDataException("Guerrilla's territories.xtbl has no terr01");
        var districts = new List<(string Name, string Guerrilla)>();
        var entries = new StringBuilder();
        foreach (Match d in Regex.Matches(terr01, @"<District>.*?</District>", RegexOptions.Singleline))
        {
            string entry = d.Value.Replace("\r\n", "\n");
            string guerrilla = Regex.Match(entry, @"<Name>([^<]*)</Name>").Groups[1].Value;
            // Two letters: the first of each word, else the first two (End Game EG, Eos Eo).
            string[] words = guerrilla.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            string name = words.Length > 1 ? $"{char.ToUpperInvariant(words[0][0])}{char.ToUpperInvariant(words[1][0])}" : guerrilla[..Math.Min(2, guerrilla.Length)];
            if (districts.Any(o => string.Equals(o.Name, name, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidDataException($"two of Guerrilla's districts would be named {name}");
            districts.Add((name, guerrilla));
            entry = new Regex(@"<Name>[^<]*</Name>").Replace(entry, $"<Name>{name}</Name>", 1);
            entry = Regex.Replace(entry, @"\s*<thumbnail>.*?</thumbnail>", "", RegexOptions.Singleline);
            entry = Regex.Replace(entry, @"\s*<load_images>.*?</load_images>", "", RegexOptions.Singleline);
            entry = Regex.Replace(entry, @"<District_Flags>.*?</District_Flags>", "<District_Flags>\n\t\t\t\t\t</District_Flags>", RegexOptions.Singleline);
            entries.Append("\t\t\t").Append(entry.Trim()).Append('\n');
        }
        if (districts.Count == 0) throw new InvalidDataException("Guerrilla's terr01 has no districts");
        if (districts.Count > 8) throw new InvalidDataException($"Guerrilla's terr01 has {districts.Count} districts; Armageddon keeps nine time-of-day sets, the default's among them");
        if (districts.Sum(d => NameRoom(d.Name)) > NamePool)
            throw new InvalidDataException($"the districts' names take {districts.Sum(d => NameRoom(d.Name))} bytes; Armageddon has room for {NamePool}");

        string territory = $"\t<Territory>\n\t\t<Name>{world}</Name>\n\t\t<Districts>\n{entries}\t\t\t</Districts>\n" +
                           "\t\t<StreamingParams>\n\t\t\t<VerticalMask></VerticalMask>\n\t\t\t<HorizMask></HorizMask>\n\t\t\t</StreamingParams>\n" +
                           "\t\t<territory_index>0</territory_index>\n\t\t<_Editor>\n\t\t\t<Category>Entries</Category>\n\t\t\t</_Editor>\n\t\t</Territory>\n";
        // Added as the table's last entry, on the lines before its closing tag.
        int end = ours.IndexOf("</Table>", StringComparison.Ordinal);
        if (end < 0) throw new InvalidDataException("Armageddon's territories.xtbl has no table");
        int at = ours.LastIndexOf('\n', end) + 1;
        string newline = ours.Contains("\r\n") ? "\r\n" : "\n";
        return (Encoding.Latin1.GetBytes(ours[..at] + territory.Replace("\n", newline) + ours[at..]), districts);
    }
}
