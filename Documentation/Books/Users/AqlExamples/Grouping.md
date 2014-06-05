<a name="grouping"></a>
# Grouping

To group results by arbitrary criteria, AQL provides the `COLLECT` keyword.
`COLLECT` will perform a grouping, but no aggregation. Aggregation can still be
added in the query if required.

<a name="grouping_by_criteria"></a>
### Grouping by criteria

To group users by age, and result the names of the users with the highest ages,
we'll issue a query like this:

    FOR u IN users 
      FILTER u.active == true 
      COLLECT age = u.age INTO usersByAge 
      SORT age DESC LIMIT 0, 5 
      RETURN { 
        "age" : age, 
        "users" : usersByAge[*].u.name 
      }

    [ 
      { 
        "age" : 37, 
        "users" : [ 
          "John", 
          "Sophia" 
        ] 
      }, 
      { 
        "age" : 36, 
        "users" : [ 
          "Fred", 
          "Emma" 
        ] 
      }, 
      { 
        "age" : 34, 
        "users" : [ 
          "Madison" 
        ] 
      }, 
      { 
        "age" : 33, 
        "users" : [ 
          "Chloe", 
          "Michael" 
        ] 
      }, 
      { 
        "age" : 32, 
        "users" : [ 
          "Alexander" 
        ] 
      } 
    ]

The query will put all users together by their `age` attribute. There will be one
result document per distinct `age` value (let aside the `LIMIT`). For each group,
we have access to the matching document via the `usersByAge` variable introduced in
the `COLLECT` statement. 

<a name="list_expander"></a>
### list expander


The `usersByAge` variable contains the full documents found, and as we're only 
interested in user names, we'll use the list expander (`[*]`) to extract just the 
`name` attribute of all user documents in each group.

The `[*]` expander is just a handy short-cut. Instead of `usersByAge[*].u.name` we 
could also write:

    FOR temp IN usersByAge
      RETURN temp.u.name

<a name="grouping_by_multiple_criteria"></a>
### Grouping by multiple criteria

To group by multiple criteria, we'll use multiple arguments in the `COLLECT` clause.
For example, to group users by `ageGroup` (a derived value we need to calculate first)
and then by `gender`, we'll do:
    
    FOR u IN users 
      FILTER u.active == true
      COLLECT ageGroup = FLOOR(u.age / 5) * 5, 
              gender = u.gender INTO group
      SORT ageGroup DESC
      RETURN { 
        "ageGroup" : ageGroup, 
        "gender" : gender 
      }

    [ 
      { 
        "ageGroup" : 35, 
        "gender" : "f" 
      }, 
      { 
        "ageGroup" : 35, 
        "gender" : "m" 
      }, 
      { 
        "ageGroup" : 30, 
        "gender" : "f" 
      }, 
      { 
        "ageGroup" : 30, 
        "gender" : "m" 
      }, 
      { 
        "ageGroup" : 25, 
        "gender" : "f" 
      }, 
      { 
        "ageGroup" : 25, 
        "gender" : "m" 
      } 
    ]

<a name="aggregation"></a>
### Aggregation

So far we only grouped data without aggregation. Adding aggregation is simple in AQL,
as all that needs to be done is to run an aggregate function on the list created by
the `INTO` clause of a `COLLECT` statement:

    FOR u IN users 
      FILTER u.active == true
      COLLECT ageGroup = FLOOR(u.age / 5) * 5, 
              gender = u.gender INTO group
      SORT ageGroup DESC
      RETURN { 
        "ageGroup" : ageGroup, 
        "gender" : gender, 
        "numUsers" : LENGTH(group) 
      }

    [ 
      { 
        "ageGroup" : 35, 
        "gender" : "f", 
        "numUsers" : 2 
      }, 
      { 
        "ageGroup" : 35, 
        "gender" : "m", 
        "numUsers" : 2 
      }, 
      { 
        "ageGroup" : 30, 
        "gender" : "f", 
        "numUsers" : 4 
      }, 
      { 
        "ageGroup" : 30, 
        "gender" : "m", 
        "numUsers" : 4 
      }, 
      { 
        "ageGroup" : 25, 
        "gender" : "f", 
        "numUsers" : 2 
      }, 
      { 
        "ageGroup" : 25, 
        "gender" : "m", 
        "numUsers" : 2 
      } 
    ]

We have used the function `LENGTH` (returns the length of a list) here. This is the
equivalent to SQL's `SELECT g, COUNT(*) FROM ... GROUP BY g`.
In addition to `LENGTH` AQL also provides `MAX`, `MIN`, `SUM` and `AVERAGE` as 
basic aggregation functions.

In AQL all aggregation functions can be run on lists only. If an aggregation function
is run on anything that is not a list, an error will be occure and the query will fail.

<a name="post-filtering_aggregated_data"></a>
### Post-filtering aggregated data

To filter on the results of a grouping or aggregation operation (i.e. something
similar to `HAVING` in SQL), simply add another `FILTER` clause after the `COLLECT` 
statement. 

For example, to get the 3 `ageGroup`s with the most users in them:

    FOR u IN users 
      FILTER u.active == true 
      COLLECT ageGroup = FLOOR(u.age / 5) * 5 INTO group 
      LET numUsers = LENGTH(group) 
      FILTER numUsers > 2 // group must contain at least 3 users in order to qualify 
      SORT numUsers DESC 
      LIMIT 0, 3 
      RETURN { 
        "ageGroup" : ageGroup, 
        "numUsers" : numUsers, 
        "users" : group[*].u.name 
      }

    [ 
      { 
        "ageGroup" : 30, 
        "numUsers" : 8, 
        "users" : [ 
          "Abigail", 
          "Madison", 
          "Anthony", 
          "Alexander", 
          "Isabella", 
          "Chloe", 
          "Daniel", 
          "Michael" 
        ] 
      }, 
      { 
        "ageGroup" : 25, 
        "numUsers" : 4, 
        "users" : [ 
          "Mary", 
          "Mariah", 
          "Jim", 
          "Diego" 
        ] 
      }, 
      { 
        "ageGroup" : 35, 
        "numUsers" : 4, 
        "users" : [ 
          "Fred", 
          "John", 
          "Emma", 
          "Sophia" 
        ] 
      } 
    ]

To increase readability, the repeated expression `LENGTH(group)` was put into a variable
`numUsers`. The `FILTER` on `numUsers` is the SQL HAVING equivalent.
