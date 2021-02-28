# rt-price-thermostat

Skeleton code for smart thermostat.  
Google has now allowed API access to Nest thermostats, so this project is dead in it’s current form.

Fetches real-time price from griddy and ercot.

Connects to si7021 to read temperature
Sets up webserver with mDNS at “griddy.local” address.  Expects filesystem with /index.html file

Saves/restores options from flash ram on reboot.
