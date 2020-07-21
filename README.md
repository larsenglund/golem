# Golem
A open controller for electric kilns.

## Firing/temperature profiles
A firing profile is described like this:

Segment number | Rate (°C / hr) | Target temperature (°C) | Stage time (hr)
-------------- | -------------- | ----------------------- | ----------
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

## Todo
* Add file browser for uploading, downloading, viewing and editing raw JSON-files. ACE might be a good editor to embed.
