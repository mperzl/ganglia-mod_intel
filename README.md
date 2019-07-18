# ganglia-mod_intel: Ganglia Monitor daemon module intel

This gmond module provides additional useful Intel metrics.

**License:** BSD 3-Clause "New" or "Revised" License

The following additional metrics are provided for Linux on Intel:
* `cpu_entitlement`
* `cpu_in_lpar`
* `cpu_type`
* `cpu_used`
* `disk_iops`
* `disk_read`
* `disk_write`
* `fwversion`
* `kernel64bit`
* `kvm_guest`
* `model_name`
* `oslevel`
* `serial_num`

## Obtaining binary versions

Downloads of binary versions for AIX and Linux on Power are available at:

    http://www.perzl.org/aix/index.php?n=Main.Ganglia

## Description of individual metrics

Metric:	**`cpu_entitlement`**

**Return type:** `GANGLIA_VALUE_FLOAT`

* This metric returns the Capacity Entitlement of the system in units of physical cores.

----

Metric:	**`cpu_in_lpar`**

**Return type:** `GANGLIA_VALUE_UNSIGNED_INT`

* This metric returns the number of CPUs the OS sees in the system.

----

Metric:	**`cpu_type`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric returns the CPU model name.

----

Metric:	**`cpu_used`**

**Return type:** `GANGLIA_VALUE_FLOAT`

* This metric returns in fractional numbers of physical cores used since the last time this metric was measured.
* For good numerical results the time stamps are measured in µ-seconds.

----

Metric:	**`disk_iops`**

**Return type:** `GANGLIA_VALUE_DOUBLE`

* This metric returns the total number of I/O operations per second.
* For good numerical results the time stamps are measured in µ-seconds.

----

Metric:	**`disk_read`**

**Return type:** `GANGLIA_VALUE_FLOAT`

* This metric returns the total read I/O in bytes/sec of the system.
* For good numerical results the time stamps are measured in µ-seconds.

----

Metric:	**`disk_write`**

**Return type:** `GANGLIA_VALUE_DOUBLE`

* This metric returns the total write I/O in bytes/sec of the system.
* For good numerical results the time stamps are measured in µ-seconds.

----

Metric:	**`fwversion`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric returns the the current firmware version of the system.

----

Metric:	**`kernel64bit`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric either returns **`yes`** if the running AIX kernel is a 64-bit kernel or **`no`** otherwise.

----

Metric:	**`kvm_guest`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric either returns **`yes`** if this is a KVM VM or **`no`** otherwise.

----

Metric:	**`model_name`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric returns the machine model name.

----

Metric:	**`oslevel`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric returns the exact Linux version string.
* This metric is retrieved only once and then “cached” for subsequential calls.

----

Metric:	**`serial_num`**

**Return type:** `GANGLIA_VALUE_STRING`

* This metric returns the serial number of the hardware system.
