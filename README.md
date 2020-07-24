# Golem
A open controller for electric kilns.

## Firing/temperature profiles
A firing profile is described like this:

Segment number | Rate (°C / hr) | Target temperature (°C) | Segment time (hr)
-------------- | -------------- | ----------------------- | -----------------
1 | 100 | 250 | 2.5
2 | 200 | 850 | 3

and the same profile in JSON:

```json
{
  "name" : "Raku",
  "1" : {
    "rate" : 100,
    "target" : 250
    },
  "2" : {
    "rate" : 200,
    "target" : 800
    }
}
```

but currently each profile is stored as a SPIFFS file in the root as ```/<profile name>``` and contains ```<rate>,<target>{,<rate>,<target>}```, for example:

```
80,250,50,800
```

The last open profile and the state of that firing is stored in ```/state/current``` and contains:

```
<profile name>
<segment number>
```

If segment number is zero it means the firing profile is not running.

## Todo
* Add file browser for uploading, downloading, viewing and editing raw JSON-files. ACE might be a good editor to embed.
