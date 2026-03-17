namespace HypermateViewer.Api.Configuration;

public class LinesConfiguration
{
    public static readonly IReadOnlyList<(string Id, string DisplayName)> FiringLines =
    [
        ("firing4", "Forno 4"),
        ("firing5", "Forno 5"),
        ("firing6", "Forno 6")
    ];

    public static readonly IReadOnlyDictionary<string, string> MachineMap =
        new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase)
        {
            { "entry", "kilnentr" },
            { "exit", "exit" }
        };
}
