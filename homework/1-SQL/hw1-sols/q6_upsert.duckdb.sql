insert into teams (yearID, lgID, teamID, attendance, teamRank)
select yearID, lgID, franchID, attendance, teamRank from teams
where yearID = 2024
on conflict do update set attendance = -1;
