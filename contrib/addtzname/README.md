# addtzname

Use at your own risk.

Basically, after taking a backup, this is:

- `o` is the path to your existing STORAGEDIR
- `n` is a new directory

```console
$ mkdir n
$ ot-recorder -S n --initialize

$ ocat -S o --dump | ./addtzname | ocat -S n --load
$ ocat -S n --dump | head  # verify JSON contains tzname

# when you're ready:
$ cp n/ghash/* ....../o/ghash/
```

