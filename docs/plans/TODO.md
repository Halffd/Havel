1. support {} shorthand for lambda functions, e.g.
numbers = { print(random.shuffle([2,4,5,8]) }
vars = (x,y){ x ** y }
2. supporrt erlang style functions:
│
fibonacci(N) if N < 2 =>
      print("Computing fibonacci $N")
fibonacci(N) =>
   print("Computing fibonacci $N"),
   fibonacci(N - 1) + fibonacci(N - 2)
3. switch syntax:
case txt ~ name of {
    0 -> ok
    _ -> print("$name in $txt")
}
4. Implement elif syntax
5. Implement receive { } syntax
