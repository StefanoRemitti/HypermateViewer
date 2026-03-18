using System.Text.Json;
using HypermateViewer.Api.Models;
using Microsoft.Data.SqlClient;

namespace HypermateViewer.Api.Services;

public class FiringService : IFiringService
{
    private readonly string _connectionString;

    public FiringService(IConfiguration configuration)
    {
        _connectionString = configuration.GetConnectionString("TrackingLog")
            ?? throw new InvalidOperationException("Connection string 'TrackingLog' not found.");
    }

    public async Task<CalledOrderResult?> GetLastCalledOrderAsync(string line)
    {
        const string sql = """
            SELECT TOP 1
                LEFT(RTRIM(CONVERT(DATETIMEOFFSET, p.EventTime)), 19) AS EventTime,
                SUBSTRING(p.Message, 49, 7) AS ErpCode
            FROM [TrackingLog].[dbo].[PrimeLogs] p
            WHERE p.EventTime >= DATEADD(HOUR, -48, SYSDATETIME())
              AND p.ServiceName LIKE 'Prime.Bridge.CoopImola.Import'
              AND p.Message LIKE 'CallOrderWithConnector OK. operationOrderCode: %, billOfResourcesName: ''' + @line + '''.'
            ORDER BY p.EventTime DESC;
            """;

        await using var conn = new SqlConnection(_connectionString);
        await conn.OpenAsync();
        await using var cmd = new SqlCommand(sql, conn);
        cmd.Parameters.AddWithValue("@line", line);

        await using var reader = await cmd.ExecuteReaderAsync();
        if (!await reader.ReadAsync()) return null;

        var erpCode = reader.GetString(1);
        return new CalledOrderResult
        {
            EventTime = reader.GetString(0),
            ErpCode = erpCode,
            OrderNumber = ExtractOrderNumber(erpCode, '_')
        };
    }

    public async Task<ActiveOrderResult?> GetActiveOrderAsync(string line, string machine)
    {
        const string sql = """
            SELECT TOP 1
                LEFT(RTRIM(CONVERT(DATETIMEOFFSET, p.EventTime)), 19) AS EventTime,
                ExtractedJson =
                (
                    SELECT
                        RigaOrdineHypermate = JSON_VALUE(i.value, '$.OperationOrderRowId'),
                        CodiceOrdine        = JSON_VALUE(i.value, '$.VariableValue')
                    FROM OPENJSON(j.clean_json, '$.I') i
                    WHERE JSON_VALUE(i.value, '$.VariableName') = 'OperationOrderRowCustomCode'
                    FOR JSON PATH, WITHOUT_ARRAY_WRAPPER
                )
            FROM [TrackingLog].[dbo].[PrimeLogs] p
            OUTER APPLY (
                SELECT json_part =
                    CASE
                        WHEN p.ServiceName LIKE 'Prime.LinesManagement.' + @line
                        THEN SUBSTRING(p.Message, CHARINDEX('{', p.Message), LEN(p.Message))
                    END
            ) s
            OUTER APPLY (
                SELECT clean_json =
                    CASE
                        WHEN s.json_part IS NOT NULL
                        THEN LEFT(s.json_part, LEN(s.json_part) - CHARINDEX('}', REVERSE(s.json_part)) + 1)
                    END
            ) j
            WHERE p.EventTime >= DATEADD(HOUR, -48, SYSDATETIME())
              AND p.ServiceName LIKE 'Prime.LinesManagement.' + @line
              AND p.Message LIKE 'Operation: Execution Operation SetNewOrderRow%: Call Machine: SetGenericCommand(GenericCommand.Order, GenericCommandType.Set, {"A":% on ' + @line + '.' + @machine + '%service%'
            ORDER BY p.EventTime DESC;
            """;

        await using var conn = new SqlConnection(_connectionString);
        await conn.OpenAsync();
        await using var cmd = new SqlCommand(sql, conn);
        cmd.Parameters.AddWithValue("@line", line);
        cmd.Parameters.AddWithValue("@machine", machine);

        await using var reader = await cmd.ExecuteReaderAsync();
        if (!await reader.ReadAsync()) return null;

        var eventTime = reader.GetString(0);
        var extractedJson = reader.IsDBNull(1) ? null : reader.GetString(1);

        string rigaOrdine = string.Empty;
        string codiceOrdine = string.Empty;

        if (!string.IsNullOrEmpty(extractedJson))
        {
            var doc = JsonDocument.Parse(extractedJson);
            var root = doc.RootElement;
            rigaOrdine = root.TryGetProperty("RigaOrdineHypermate", out var riga) ? riga.GetString() ?? string.Empty : string.Empty;
            codiceOrdine = root.TryGetProperty("CodiceOrdine", out var codice) ? codice.GetString() ?? string.Empty : string.Empty;
        }

        return new ActiveOrderResult
        {
            EventTime = eventTime,
            RigaOrdineHypermate = rigaOrdine,
            CodiceOrdine = codiceOrdine,
            OrderNumber = ExtractOrderNumber(codiceOrdine, '-')
        };
    }

    public async Task<CountersActivationResult?> GetCountersActivationAsync(string line)
    {
        // The double space between 'ID=%' and 'MachineName' is intentional —
        // it matches the actual message format produced by the PLC firmware.
        const string sql = """
            SELECT TOP 1
                LEFT(RTRIM(CONVERT(DATETIMEOFFSET, EventTime)), 22) AS EventTime,
                CASE
                    WHEN CHARINDEX('ErpCode ', Message) > 0
                     AND CHARINDEX('-', Message, CHARINDEX('ErpCode ', Message)) > CHARINDEX('ErpCode ', Message) + 8
                    THEN RTRIM(SUBSTRING(Message, CHARINDEX('ErpCode ', Message) + 8,
                             CHARINDEX('-', Message, CHARINDEX('ErpCode ', Message)) - (CHARINDEX('ErpCode ', Message) + 8)))
                    ELSE ''
                END AS ErpCode
            FROM [TrackingLog].[dbo].[PrimeLogs]
            WHERE EventTime >= DATEADD(HOUR, -48, SYSDATETIME())
              AND ServiceName = 'Prime.LinesManagement.' + @line
              AND Message LIKE 'Command: New Command to Active and Execute: RequestOrderActivation% ID=%  MachineName ' + @line + '.KilnEntry% - WF BorName ' + @line + '%'
            ORDER BY EventTime DESC;
            """;

        await using var conn = new SqlConnection(_connectionString);
        await conn.OpenAsync();
        await using var cmd = new SqlCommand(sql, conn);
        cmd.Parameters.AddWithValue("@line", line);

        await using var reader = await cmd.ExecuteReaderAsync();
        if (!await reader.ReadAsync()) return null;

        var erpCode = reader.IsDBNull(1) ? string.Empty : reader.GetString(1);
        return new CountersActivationResult
        {
            EventTime = reader.GetString(0),
            ErpCode = erpCode,
            OrderNumber = ExtractOrderNumber(erpCode, '_')
        };
    }

    public async Task<IEnumerable<CounterResult>> GetCountersByLineAsync(string line, string moErpCode)
    {
        const string sql = """
            SELECT
                LEFT(RTRIM(CONVERT(DATETIMEOFFSET, MAX(p.EventTime))), 19) AS LastPiece,
                CASE WHEN m.IsInBound = 1 THEN 'Inbound' ELSE 'Outbound' END AS Machine,
                SUM(p.Pieces) AS Pieces,
                FORMAT(SUM(ISNULL(p.Pieces * it.PiecesToSqmFactor, 0)), '#.##') AS m2
            FROM [EventSourcing].[Fact].[Productions] p
            JOIN [EventSourcing].[Dim].[Machines] m ON m.PrimeMachineName = p.PrimeMachineName
            LEFT JOIN PrimeOperationManagement.dbo.OperationOrderRows oor ON p.OperationOrderRowOperationManagementID = oor.OperationOrderRowID
            LEFT JOIN PrimeOperationManagement.dbo.OperationOrders oo ON oo.OperationOrderID = oor.OperationOrderID
            LEFT JOIN PrimeOperationManagement.dbo.MasterOrders mo ON mo.MasterOrderID = oo.MasterOrderID
            LEFT JOIN PrimeOperationManagement.dbo.Items it ON it.ItemID = oor.ItemID
            WHERE m.PrimeMachineName LIKE @line + '%'
              AND (m.IsInBound = 1 OR m.IsOutBound = 1)
              AND (@moErpCode = '' OR mo.ErpCode = @moErpCode)
              AND p.StationType = 0
            GROUP BY m.PrimeMachineName, p.OperationOrderRowOperationManagementID, m.IsInBound, mo.ErpCode, it.PiecesToSqmFactor
            ORDER BY m.IsInBound DESC;
            """;

        return await ExecuteCountersQueryAsync(sql, line, moErpCode);
    }

    public async Task<IEnumerable<CounterResult>> GetCountersByTimeframeAsync(string line, string moErpCode, DateTime startTime)
    {
        const string sql = """
            SELECT
                LEFT(RTRIM(CONVERT(DATETIMEOFFSET, MAX(p.EventTime))), 19) AS LastPiece,
                CASE WHEN m.IsInBound = 1 THEN 'Inbound' ELSE 'Outbound' END AS Machine,
                SUM(p.Pieces) AS Pieces,
                FORMAT(SUM(ISNULL(p.Pieces * it.PiecesToSqmFactor, 0)), '#.##') AS m2
            FROM [EventSourcing].[Fact].[Productions] p
            JOIN [EventSourcing].[Dim].[Machines] m ON m.PrimeMachineName = p.PrimeMachineName
            LEFT JOIN PrimeOperationManagement.dbo.OperationOrderRows oor ON p.OperationOrderRowOperationManagementID = oor.OperationOrderRowID
            LEFT JOIN PrimeOperationManagement.dbo.OperationOrders oo ON oo.OperationOrderID = oor.OperationOrderID
            LEFT JOIN PrimeOperationManagement.dbo.MasterOrders mo ON mo.MasterOrderID = oo.MasterOrderID
            LEFT JOIN PrimeOperationManagement.dbo.Items it ON it.ItemID = oor.ItemID
            WHERE p.EventTime >= @startTime
              AND m.PrimeMachineName LIKE @line + '%'
              AND (m.IsInBound = 1 OR m.IsOutBound = 1)
              AND (@moErpCode = '' OR mo.ErpCode = @moErpCode)
              AND p.StationType = 0
            GROUP BY m.PrimeMachineName, p.OperationOrderRowOperationManagementID, m.IsInBound, mo.ErpCode, it.PiecesToSqmFactor
            ORDER BY m.IsInBound DESC;
            """;

        await using var conn = new SqlConnection(_connectionString);
        await conn.OpenAsync();
        await using var cmd = new SqlCommand(sql, conn);
        cmd.Parameters.AddWithValue("@line", line);
        cmd.Parameters.AddWithValue("@moErpCode", moErpCode ?? string.Empty);
        cmd.Parameters.Add("@startTime", System.Data.SqlDbType.DateTime2).Value = startTime;

        return await ReadCountersAsync(cmd);
    }

    private async Task<IEnumerable<CounterResult>> ExecuteCountersQueryAsync(string sql, string line, string moErpCode)
    {
        await using var conn = new SqlConnection(_connectionString);
        await conn.OpenAsync();
        await using var cmd = new SqlCommand(sql, conn);
        cmd.Parameters.AddWithValue("@line", line);
        cmd.Parameters.AddWithValue("@moErpCode", moErpCode ?? string.Empty);

        return await ReadCountersAsync(cmd);
    }

    private static async Task<IEnumerable<CounterResult>> ReadCountersAsync(SqlCommand cmd)
    {
        var results = new List<CounterResult>();
        await using var reader = await cmd.ExecuteReaderAsync();
        while (await reader.ReadAsync())
        {
            results.Add(new CounterResult
            {
                LastPiece = reader.IsDBNull(0) ? string.Empty : reader.GetString(0),
                Machine = reader.IsDBNull(1) ? string.Empty : reader.GetString(1),
                Pieces = reader.IsDBNull(2) ? 0 : reader.GetInt32(2),
                M2 = reader.IsDBNull(3) ? string.Empty : reader.GetString(3)
            });
        }
        return results;
    }

    private static string ExtractOrderNumber(string code, char separator)
    {
        if (string.IsNullOrEmpty(code)) return string.Empty;
        var idx = code.IndexOf(separator);
        return idx > 0 ? code[..idx] : code;
    }
}
