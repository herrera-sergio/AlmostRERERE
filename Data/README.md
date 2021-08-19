# Almost-RERERE Data
The data in this repository are json files containing the extracted conflicts of 21 open source projects that were used to test and validate our approach.

## Repositories
- **Single Line**: Contains the extracted single line conflicts found on the analysed projects.
- **Multi Line**: Contains the extracted multi-line conflicts found on the analysed projects.
- **DB_dump**: Contains a PostgreSQL(v13) database dump with all the metadata of the 21 projects used for the evaluation of the approach. The database was created and populated using the tools proposed in ["On the  nature of merge conflicts: a study of 2,731 open source java projects hosted by github"](https://gems-uff.github.io/merge-nature/). *Notice: This database is not require for replicating the experiment, it might be helpful for further analysis or other similar studies*