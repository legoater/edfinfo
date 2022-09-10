# edfinfo

edfinfo is a server daemon reading telemetry information [1] from
electricity meters in France, including the newer Linky meter.
Various telemetry frames are sent on a serial line with an output rate
of approximately 1 frame per second.

To avoid data bloat, edfinfo filters redundant frames before sending
them to a MySQL server and/or a MQTT broker.

[1] http://www.enedis.fr/sites/default/files/Enedis-NOI-CPT_54E.pdf

## Compile & install

The Mysql, Mosquitto and Inih libraries and development headers are
required to compile. On Redhat systems, these should be available in
packages : libinih-dev, mosquitto-dev, libmysqlclient-dev and on
Debian systems : inih-devel, mosquitto-devel, mariadb-devel .

Run :

```
make && make install
```

## Configuration

See edfinfo man page for more information on configuration.

## Links

 * http://www.magdiblog.fr/gpio/teleinfo-edf-suivi-conso-de-votre-compteur-electrique/
 * http://hallard.me/teleinfo/
 * http://play.with.free.fr/index.php/suivi-de-consommation-electrique-avec-telinfuse-et-openenergymonitor/

