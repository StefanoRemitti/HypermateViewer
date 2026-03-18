using HypermateViewer.Api.Models;
using Microsoft.Data.SqlClient;

namespace HypermateViewer.Api.Services;

public class LogService : ILogService
{
    private readonly string _connectionString;

    public LogService(IConfiguration configuration)
    {
        _connectionString = configuration.GetConnectionString("HypermateViewer")
            ?? throw new InvalidOperationException("Connection string 'HypermateViewer' not found.");
    }

    public async Task InsertLogAsync(LogEntry entry)
    {
        var now = DateTime.Now;
        var dateId = int.Parse(now.ToString("yyyyMMdd"));

        const string sql = """
            INSERT INTO [dbo].[Logs]
                ([DateId], [EventTime], [Line], [Machine], [StepDescription], [OldState], [NewState], [OldErpCode], [NewErpCode])
            VALUES
                (@DateId, @EventTime, @Line, @Machine, @StepDescription, @OldState, @NewState, @OldErpCode, @NewErpCode)
            """;

        await using var conn = new SqlConnection(_connectionString);
        await conn.OpenAsync();
        await using var cmd = new SqlCommand(sql, conn);
        cmd.Parameters.AddWithValue("@DateId", dateId);
        cmd.Parameters.Add("@EventTime", System.Data.SqlDbType.DateTime2).Value = now;
        cmd.Parameters.AddWithValue("@Line", entry.Line);
        cmd.Parameters.AddWithValue("@Machine", entry.Machine);
        cmd.Parameters.AddWithValue("@StepDescription", entry.StepDescription);
        cmd.Parameters.AddWithValue("@OldState", entry.OldState);
        cmd.Parameters.AddWithValue("@NewState", entry.NewState);
        cmd.Parameters.AddWithValue("@OldErpCode", entry.OldErpCode);
        cmd.Parameters.AddWithValue("@NewErpCode", entry.NewErpCode);
        await cmd.ExecuteNonQueryAsync();
    }

    public async Task<IEnumerable<LogRecord>> GetLogsAsync(LogFilter filter)
    {
        var conditions = new List<string>();
        if (!string.IsNullOrEmpty(filter.Line))    conditions.Add("AND [Line] = @Line");
        if (!string.IsNullOrEmpty(filter.ErpCode)) conditions.Add("AND ([OldErpCode] = @ErpCode OR [NewErpCode] = @ErpCode)");
        if (filter.DateFrom.HasValue)              conditions.Add("AND [EventTime] >= @DateFrom");
        if (filter.DateTo.HasValue)                conditions.Add("AND [EventTime] <= @DateTo");
        if (!string.IsNullOrEmpty(filter.State))   conditions.Add("AND ([OldState] = @State OR [NewState] = @State)");

        var sql = $"""
            SELECT [DateId], [EventTime], [Line], [Machine], [StepDescription], [OldState], [NewState], [OldErpCode], [NewErpCode]
            FROM [dbo].[Logs]
            WHERE 1=1
            {string.Join(" ", conditions)}
            ORDER BY [EventTime] DESC
            """;

        await using var conn = new SqlConnection(_connectionString);
        await conn.OpenAsync();
        await using var cmd = new SqlCommand(sql, conn);

        if (!string.IsNullOrEmpty(filter.Line))    cmd.Parameters.AddWithValue("@Line", filter.Line);
        if (!string.IsNullOrEmpty(filter.ErpCode)) cmd.Parameters.AddWithValue("@ErpCode", filter.ErpCode);
        if (filter.DateFrom.HasValue)              cmd.Parameters.Add("@DateFrom", System.Data.SqlDbType.DateTime2).Value = filter.DateFrom.Value;
        if (filter.DateTo.HasValue)                cmd.Parameters.Add("@DateTo", System.Data.SqlDbType.DateTime2).Value = filter.DateTo.Value;
        if (!string.IsNullOrEmpty(filter.State))   cmd.Parameters.AddWithValue("@State", filter.State);

        var results = new List<LogRecord>();
        await using var reader = await cmd.ExecuteReaderAsync();
        while (await reader.ReadAsync())
        {
            results.Add(new LogRecord
            {
                DateId        = reader.GetInt32(0),
                EventTime     = reader.GetDateTime(1),
                Line          = reader.GetString(2),
                Machine       = reader.GetString(3),
                StepDescription = reader.GetString(4),
                OldState      = reader.GetString(5),
                NewState      = reader.GetString(6),
                OldErpCode    = reader.GetString(7),
                NewErpCode    = reader.GetString(8)
            });
        }
        return results;
    }
}
