namespace HypermateViewer.Api.Models;

public class LogFilter
{
    public string? Line { get; set; }
    public string? ErpCode { get; set; }
    public DateTime? DateFrom { get; set; }
    public DateTime? DateTo { get; set; }
    public string? State { get; set; }
}
