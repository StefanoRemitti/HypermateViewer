using HypermateViewer.Api.Models;

namespace HypermateViewer.Api.Services;

public interface IFiringService
{
    Task<CalledOrderResult?> GetLastCalledOrderAsync(string line);
    Task<ActiveOrderResult?> GetActiveOrderAsync(string line, string machine);
    Task<CountersActivationResult?> GetCountersActivationAsync(string line);
    Task<IEnumerable<CounterResult>> GetCountersByLineAsync(string line, string moErpCode);
    Task<IEnumerable<CounterResult>> GetCountersByTimeframeAsync(string line, string moErpCode, DateTime startTime);
}
