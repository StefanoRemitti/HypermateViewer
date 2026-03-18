using HypermateViewer.Api.Models;

namespace HypermateViewer.Api.Services;

public interface ILogService
{
    Task InsertLogAsync(LogEntry entry);
    Task<IEnumerable<LogRecord>> GetLogsAsync(LogFilter filter);
    Task<LogRecord?> GetLatestLogAsync(string line, string stepDescription);
}
