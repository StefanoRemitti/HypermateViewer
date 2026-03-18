using HypermateViewer.Api.Models;
using HypermateViewer.Api.Services;
using Microsoft.AspNetCore.Mvc;

namespace HypermateViewer.Api.Controllers;

[ApiController]
[Route("api/logs")]
public class LogsController : ControllerBase
{
    private readonly ILogService _logService;

    public LogsController(ILogService logService)
    {
        _logService = logService;
    }

    /// <summary>Inserts a state-change log entry.</summary>
    [HttpPost]
    public async Task<IActionResult> InsertLog([FromBody] LogEntry entry)
    {
        await _logService.InsertLogAsync(entry);
        return Ok();
    }

    /// <summary>Returns log entries filtered by the provided criteria.</summary>
    [HttpGet]
    public async Task<ActionResult<IEnumerable<LogRecord>>> GetLogs(
        [FromQuery] string? line = null,
        [FromQuery] string? erpCode = null,
        [FromQuery] DateTime? dateFrom = null,
        [FromQuery] DateTime? dateTo = null,
        [FromQuery] string? state = null)
    {
        var filter = new LogFilter
        {
            Line     = line,
            ErpCode  = erpCode,
            DateFrom = dateFrom,
            DateTo   = dateTo,
            State    = state
        };

        var results = await _logService.GetLogsAsync(filter);
        return Ok(results);
    }

    /// <summary>Returns the latest log entry for a given line and step description.</summary>
    [HttpGet("latest")]
    public async Task<ActionResult<LogRecord?>> GetLatestLog(
        [FromQuery] string line,
        [FromQuery] string stepDescription)
    {
        if (string.IsNullOrWhiteSpace(line) || string.IsNullOrWhiteSpace(stepDescription))
            return BadRequest("Both 'line' and 'stepDescription' query parameters are required.");

        var result = await _logService.GetLatestLogAsync(line, stepDescription);
        if (result == null) return NoContent();
        return Ok(result);
    }
}
