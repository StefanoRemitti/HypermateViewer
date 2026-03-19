using HypermateViewer.Api.Configuration;
using HypermateViewer.Api.Models;
using HypermateViewer.Api.Services;
using Microsoft.AspNetCore.Mvc;

namespace HypermateViewer.Api.Controllers;

[ApiController]
[Route("api/firing")]
public class FiringController : ControllerBase
{
    private readonly IFiringService _firingService;

    public FiringController(IFiringService firingService)
    {
        _firingService = firingService;
    }

    /// <summary>Returns the list of available firing lines.</summary>
    [HttpGet("lines")]
    public ActionResult<IEnumerable<LineInfo>> GetLines()
    {
        var lines = LinesConfiguration.FiringLines
            .Select(l => new LineInfo { Id = l.Id, DisplayName = l.DisplayName });
        return Ok(lines);
    }

    /// <summary>Returns the last called order for the given line.</summary>
    [HttpGet("{line}/called-order")]
    public async Task<ActionResult<CalledOrderResult>> GetCalledOrder(string line)
    {
        if (!IsValidLine(line))
            return BadRequest($"Unknown line: {line}");

        var result = await _firingService.GetLastCalledOrderAsync(line);
        return result is null ? NoContent() : Ok(result);
    }

    /// <summary>Returns the active order for the given line and machine (entry/exit).</summary>
    [HttpGet("{line}/active-order/{machine}")]
    public async Task<ActionResult<ActiveOrderResult>> GetActiveOrder(string line, string machine)
    {
        if (!IsValidLine(line))
            return BadRequest($"Unknown line: {line}");

        if (!LinesConfiguration.MachineMap.TryGetValue(machine, out var dbMachine))
            return BadRequest($"Unknown machine: {machine}. Use 'entry' or 'exit'.");

        var result = await _firingService.GetActiveOrderAsync(line, dbMachine);
        return result is null ? NoContent() : Ok(result);
    }

    /// <summary>Returns the counters activation event for the given line.</summary>
    [HttpGet("{line}/counters-activation")]
    public async Task<ActionResult<CountersActivationResult>> GetCountersActivation(string line)
    {
        if (!IsValidLine(line))
            return BadRequest($"Unknown line: {line}");

        var result = await _firingService.GetCountersActivationAsync(line);
        return result is null ? NoContent() : Ok(result);
    }

    /// <summary>Returns overall counters for the given line and order code.</summary>
    [HttpGet("{line}/counters")]
    public async Task<ActionResult<IEnumerable<CounterResult>>> GetCounters(
        string line,
        [FromQuery] string moErpCode = "",
        [FromQuery] DateTime? endTime = null)
    {
        if (!IsValidLine(line))
            return BadRequest($"Unknown line: {line}");

        var results = await _firingService.GetCountersByLineAsync(line, moErpCode, endTime);
        return Ok(results);
    }

    /// <summary>Returns live counters for the given line, order code and start time.</summary>
    [HttpGet("{line}/counters/live")]
    public async Task<ActionResult<IEnumerable<CounterResult>>> GetLiveCounters(
        string line,
        [FromQuery] string moErpCode = "",
        [FromQuery] DateTime? startTime = null)
    {
        if (!IsValidLine(line))
            return BadRequest($"Unknown line: {line}");

        var from = startTime ?? DateTime.UtcNow.AddHours(-48);
        var results = await _firingService.GetCountersByTimeframeAsync(line, moErpCode, from);
        return Ok(results);
    }

    private static bool IsValidLine(string line) =>
        LinesConfiguration.FiringLines.Any(l => string.Equals(l.Id, line, StringComparison.OrdinalIgnoreCase));
}
