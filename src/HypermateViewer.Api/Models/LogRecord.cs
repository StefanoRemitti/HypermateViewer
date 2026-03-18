namespace HypermateViewer.Api.Models;

public class LogRecord
{
    public int DateId { get; set; }
    public DateTime EventTime { get; set; }
    public string Line { get; set; } = string.Empty;
    public string Machine { get; set; } = string.Empty;
    public string StepDescription { get; set; } = string.Empty;
    public string OldState { get; set; } = string.Empty;
    public string NewState { get; set; } = string.Empty;
    public string OldErpCode { get; set; } = string.Empty;
    public string NewErpCode { get; set; } = string.Empty;
}
