# Sensor Daemon Monitoring and Troubleshooting Guide

## Overview

The sensor daemon provides comprehensive monitoring and health check capabilities to ensure reliable operation and facilitate troubleshooting. This guide covers all monitoring features, health check endpoints, diagnostic tools, and troubleshooting procedures.

## Health Monitoring System

### Health Status Levels

The daemon uses four health status levels:

- **HEALTHY**: All systems operating normally
- **WARNING**: Some issues detected but system still functional
- **CRITICAL**: Serious issues that may affect functionality
- **FAILED**: System is not functioning properly

### Built-in Health Checks

The daemon automatically monitors:

1. **Memory Usage**: Tracks RAM consumption and alerts if exceeding thresholds
2. **CPU Usage**: Monitors CPU utilization and performance impact
3. **Disk Space**: Checks available storage space for data and logs
4. **Sensor Health**: Monitors I2C communication and sensor connectivity
5. **Storage Health**: Verifies database integrity and write performance

### Configuration

Health monitoring is configured in `/etc/sensor-daemon/config.toml`:

```toml
[alerts]
enabled = true
check_interval_minutes = 5
alert_cooldown_minutes = 15
max_memory_mb = 15.0
max_cpu_percent = 75.0
min_sensor_success_rate = 0.8
min_storage_success_rate = 0.95
max_i2c_failures_per_hour = 10

[monitoring]
health_endpoint_enabled = true
health_status_file = "/var/run/sensor-daemon/health.json"
health_update_interval_seconds = 60
http_server_enabled = true
http_server_port = 8080
http_server_bind_address = "127.0.0.1"
```

## Health Check Endpoints

### HTTP Endpoints

When the HTTP server is enabled, the following endpoints are available:

#### `/health` - Basic Health Status
Returns basic health information:
```json
{
  "status": "HEALTHY",
  "operational": true,
  "timestamp": 1642780800
}
```

#### `/metrics` - Detailed Metrics
Returns comprehensive system metrics:
```json
{
  "overall_status": "HEALTHY",
  "last_check": 1642780800,
  "uptime_seconds": 3600,
  "performance": {
    "memory_usage_mb": 8.5,
    "cpu_usage_percent": 12.3,
    "sensor_success_rate": 0.98,
    "storage_success_rate": 0.99,
    "i2c_connection_failures": 2
  },
  "components": [
    {
      "name": "memory",
      "status": "HEALTHY",
      "message": "Memory usage normal",
      "timestamp": 1642780800
    }
  ]
}
```

#### `/diagnostic` - Comprehensive Diagnostics
Runs full diagnostic suite and returns results as JSON.

#### `/ready` - Readiness Probe
Returns HTTP 200 if daemon is ready to serve requests, HTTP 503 if not ready.

#### `/alive` - Liveness Probe
Returns HTTP 200 if daemon process is alive and responding.

### Status File

Health status is continuously written to `/var/run/sensor-daemon/health.json` for external monitoring systems.

## Diagnostic Tools

### Command Line Diagnostic Utility

The `sensor-daemon-diagnostic` tool provides comprehensive system diagnostics:

```bash
# Run all diagnostics
sudo sensor-daemon-diagnostic

# Run specific test
sudo sensor-daemon-diagnostic i2c
sudo sensor-daemon-diagnostic storage
sudo sensor-daemon-diagnostic resources
```

Available diagnostic tests:

- **i2c**: Test I2C communication and sensor connectivity
- **storage**: Test storage engine functionality
- **resources**: Test system resources (memory, CPU, disk)
- **permissions**: Test file permissions and access rights
- **config**: Test configuration file validity
- **logging**: Test logging system functionality
- **dependencies**: Test system dependencies
- **daemon**: Test daemon process status
- **i2c-bus**: Scan I2C bus for available devices
- **sensor-quality**: Test sensor data quality
- **query-perf**: Test storage query performance

### Diagnostic Output

The diagnostic tool provides:

1. **Test Results**: Pass/fail status for each test
2. **Detailed Information**: Specific metrics and error details
3. **Troubleshooting Recommendations**: Actionable steps to resolve issues
4. **System Information**: Hardware and software environment details

## Metrics Collection

### Enhanced Metrics

The daemon collects detailed performance metrics:

#### Sensor Metrics
- Success rate over 1 hour and 24 hours
- Average reading duration
- Total reading attempts
- Error patterns and frequencies

#### Storage Metrics
- Write success rate
- Average write duration
- Database size and growth
- Query performance statistics

#### I2C Metrics
- Communication success rate
- Error code statistics
- Connection failure patterns
- Recovery attempt statistics

### Accessing Metrics

Metrics are available through:

1. **HTTP Endpoint**: `GET /metrics`
2. **Status File**: `/var/run/sensor-daemon/health.json`
3. **Log Files**: Structured logging with performance data
4. **Diagnostic Tool**: Comprehensive metrics report

## Alerting System

### Alert Types

The daemon generates alerts for:

1. **Memory Usage**: When exceeding configured thresholds
2. **CPU Usage**: When CPU utilization is too high
3. **Sensor Failures**: When sensor success rate drops
4. **Storage Issues**: When storage operations fail
5. **System Health**: When overall health degrades

### Alert Destinations

Alerts are sent to:

1. **System Logs**: Structured log entries with ERROR or CRITICAL level
2. **systemd Status**: Updates systemd service status
3. **Health Events**: Recorded in health event history

### Alert Cooldown

Alerts have configurable cooldown periods to prevent spam. Default is 15 minutes between alerts of the same type.

## Troubleshooting Procedures

### Common Issues and Solutions

#### 1. Sensor Not Connected

**Symptoms:**
- Health status shows sensor as CRITICAL or FAILED
- I2C communication errors in logs
- No sensor readings being recorded

**Diagnosis:**
```bash
sudo sensor-daemon-diagnostic i2c
sudo i2cdetect -y 1
```

**Solutions:**
- Check physical I2C connections
- Verify I2C is enabled: `sudo raspi-config`
- Check I2C device permissions: `ls -l /dev/i2c-*`
- Load I2C kernel module: `sudo modprobe i2c-dev`

#### 2. High Memory Usage

**Symptoms:**
- Memory usage alerts in logs
- Health status shows memory as WARNING or CRITICAL
- System performance degradation

**Diagnosis:**
```bash
sudo sensor-daemon-diagnostic resources
free -h
ps aux | grep sensor-daemon
```

**Solutions:**
- Check for memory leaks in logs
- Restart daemon: `sudo systemctl restart sensor-daemon`
- Adjust memory thresholds in configuration
- Monitor for memory growth patterns

#### 3. Storage Issues

**Symptoms:**
- Storage health checks failing
- Database write errors in logs
- Missing sensor data

**Diagnosis:**
```bash
sudo sensor-daemon-diagnostic storage
df -h /var/lib/sensor-daemon
sudo -u sensor-daemon ls -la /var/lib/sensor-daemon
```

**Solutions:**
- Check disk space availability
- Verify directory permissions
- Check database integrity
- Consider data retention policy adjustment

#### 4. Permission Problems

**Symptoms:**
- File access errors in logs
- Daemon fails to start
- Cannot write to data directories

**Diagnosis:**
```bash
sudo sensor-daemon-diagnostic permissions
sudo -u sensor-daemon ls -la /var/lib/sensor-daemon
```

**Solutions:**
- Fix directory ownership: `sudo chown -R sensor-daemon:sensor-daemon /var/lib/sensor-daemon`
- Check user exists: `id sensor-daemon`
- Verify systemd service user configuration

### Log Analysis

#### Log Locations

- **System Logs**: `/var/log/sensor-daemon/`
- **systemd Journal**: `journalctl -u sensor-daemon`
- **Syslog**: `/var/log/syslog` (daemon messages)

#### Important Log Patterns

**Successful Operation:**
```
[INFO] Sensor reading successful: CO2=450ppm, Temp=22.5°C, Humidity=45%
[INFO] Data stored successfully: timestamp=1642780800
```

**I2C Communication Issues:**
```
[WARN] I2C communication failed: Device not responding
[ERROR] Sensor connection lost, attempting reconnection
```

**Storage Problems:**
```
[ERROR] Database write failed: Disk full
[CRITICAL] Storage engine unhealthy: Cannot write data
```

**Memory Issues:**
```
[WARN] Memory usage above threshold: 18.5MB > 15.0MB
[CRITICAL] Memory usage critically high: 25.2MB
```

### Performance Monitoring

#### Key Performance Indicators

1. **Sensor Success Rate**: Should be > 95%
2. **Storage Success Rate**: Should be > 99%
3. **Memory Usage**: Should be < 15MB
4. **CPU Usage**: Should be < 10% average
5. **I2C Errors**: Should be < 10 per hour

#### Monitoring Commands

```bash
# Check current health status
curl http://localhost:8080/health

# Get detailed metrics
curl http://localhost:8080/metrics

# Run comprehensive diagnostics
sudo sensor-daemon-diagnostic

# Monitor system resources
htop
iotop
```

### Emergency Procedures

#### Daemon Not Responding

1. Check process status: `systemctl status sensor-daemon`
2. Check system resources: `free -h && df -h`
3. Review recent logs: `journalctl -u sensor-daemon --since "1 hour ago"`
4. Restart daemon: `sudo systemctl restart sensor-daemon`
5. If restart fails, check configuration: `sudo sensor-daemon-diagnostic config`

#### Data Loss Prevention

1. Monitor disk space regularly
2. Implement log rotation
3. Set up automated backups of configuration
4. Monitor database integrity
5. Configure appropriate data retention policies

#### System Recovery

1. Stop daemon: `sudo systemctl stop sensor-daemon`
2. Check file system integrity: `sudo fsck /dev/sda1`
3. Verify configuration: `sudo sensor-daemon-diagnostic config`
4. Clear corrupted data if necessary
5. Restart daemon: `sudo systemctl start sensor-daemon`
6. Verify operation: `sudo sensor-daemon-diagnostic`

## Integration with External Monitoring

### Prometheus Integration

The daemon can be integrated with Prometheus for advanced monitoring:

```yaml
# prometheus.yml
scrape_configs:
  - job_name: 'sensor-daemon'
    static_configs:
      - targets: ['localhost:8080']
    metrics_path: '/metrics'
    scrape_interval: 30s
```

### Nagios Integration

Create Nagios check commands:

```bash
# Check daemon health
define command{
    command_name    check_sensor_daemon_health
    command_line    /usr/lib/nagios/plugins/check_http -H localhost -p 8080 -u /ready
}

# Check sensor metrics
define command{
    command_name    check_sensor_metrics
    command_line    /usr/local/bin/check_sensor_daemon_metrics.sh
}
```

### Grafana Dashboards

Key metrics to monitor in Grafana:

1. Sensor reading success rate over time
2. Memory and CPU usage trends
3. I2C error frequency
4. Storage performance metrics
5. System health status history

## Best Practices

### Monitoring Setup

1. **Enable all health checks** in production
2. **Set appropriate thresholds** based on your environment
3. **Configure alert cooldowns** to prevent spam
4. **Monitor trends** not just current values
5. **Set up external monitoring** for redundancy

### Maintenance

1. **Regular diagnostic runs** (weekly)
2. **Log rotation** to manage disk space
3. **Configuration backups** before changes
4. **Performance baseline** establishment
5. **Documentation updates** for environment changes

### Security

1. **Restrict HTTP server access** to localhost by default
2. **Use proper file permissions** for all daemon files
3. **Regular security updates** for dependencies
4. **Monitor for unauthorized access** attempts
5. **Secure log file access** appropriately

This comprehensive monitoring and troubleshooting system ensures reliable operation of the sensor daemon and provides the tools necessary to quickly identify and resolve any issues that may arise.