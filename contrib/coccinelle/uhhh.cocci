// These are safe to apply to *.c *.h builtin/*.c

@ get_fn @
identifier fn;
@@
(
repo_config
|
git_config
|
git_protected_config
)
  (fn, ...)

@ extends get_fn @
identifier C1, C2, D;
@@
int fn(const char *C1, const char *C2,
+  struct key_value_info *kvi,
  void *D);

@ extends get_fn @
@@
int fn(const char *, const char *,
+  struct key_value_info *,
  void *);

@ extends get_fn@
identifier fn2;
identifier C1, C2, D;
attribute name UNUSED;
@@
int fn(const char *C1, const char *C2,
+  struct key_value_info *kvi,
  void *D) {
<+...
(
fn2(C1, C2,
+ kvi,
...);
|
if(fn2(C1, C2,
+ kvi,
...) < 0) { ... }
|
return fn2(C1, C2,
+ kvi,
...);
)
...+>
  }

@ extends get_fn@
identifier C1, C2, D;
attribute name UNUSED;
@@
int fn(const char *C1, const char *C2,
+  struct key_value_info *kvi UNUSED,
  void *D) {...}
