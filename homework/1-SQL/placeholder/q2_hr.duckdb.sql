with pa_players as (
select DISTINCT playerID from collegeplaying cp 
join schools 
on cp.schoolID = schools.schoolID where state='PA'
),

max_hr as (
select playerID, max(HR) as max_hr from appearances group by playerID
)

select p.nameFirst || ' (' || p.nameGiven || ') ' || p.nameLast AS name,
m.max_hr
from max_hr m
join pa_players pa
    on m.playerID = pa.playerID
join people p
    on p.playerID = m.playerID
order by 
    m.max_hr DESC,
    p.nameFirst ASC
LIMIT 10;
