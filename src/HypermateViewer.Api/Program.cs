using HypermateViewer.Api.Services;
using Microsoft.Data.SqlClient;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddControllers();

builder.Services.AddScoped<IFiringService, FiringService>();

builder.Services.AddCors(options =>
{
    options.AddDefaultPolicy(policy =>
    {
        var allowedOrigins = builder.Configuration
            .GetSection("Cors:AllowedOrigins")
            .Get<string[]>() ?? ["http://localhost:4200"];

        policy.WithOrigins(allowedOrigins)
              .AllowAnyHeader()
              .AllowAnyMethod();
    });
});

var app = builder.Build();

// Global exception handler for development
if (app.Environment.IsDevelopment())
{
    app.UseDeveloperExceptionPage();
}

app.UseCors();

// Health check endpoint to verify DB connectivity
app.MapGet("/health", async (IConfiguration config) =>
{
    try
    {
        var connStr = config.GetConnectionString("TrackingLog");
        if (string.IsNullOrEmpty(connStr))
            return Results.Json(new { status = "error", message = "Connection string 'TrackingLog' not configured" });

        await using var conn = new SqlConnection(connStr);
        await conn.OpenAsync();
        return Results.Json(new { status = "ok", database = conn.Database, server = conn.DataSource });
    }
    catch (Exception ex)
    {
        return Results.Json(new { status = "error", message = ex.Message });
    }
});

app.MapControllers();

app.Run();
