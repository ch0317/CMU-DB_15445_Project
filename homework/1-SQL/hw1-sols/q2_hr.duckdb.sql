with schoolids as (
  -- Find all schoolID in state of PA
  select schoolID from schools where state = 'PA'
),
players as (
  -- Find all players who played for a PA school
  select distinct people.playerID, nameFirst, nameGiven, nameLast
  from people, collegeplaying, schoolids
  where collegeplaying.schoolID = schoolids.schoolID and collegeplaying.playerID = people.playerID
),
stats (playerID, max_hr_appearance) as (
  select playerID, max(HR)
  from appearances
  group by playerID 
)

select (nameFirst || ' (' || nameGiven || ') ' || nameLast) as name, max_hr_appearance
from players, stats
where players.playerID = stats.playerID
order by max_hr_appearance desc, nameFirst
limit 10;
