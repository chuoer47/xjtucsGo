# Python, Pandas, and SQL

### Short Description

This workshop provides an overview of how to use Python, Pandas, and SQL to analyze data. It's aimed at programmers, analysts,
and data scientists who work with relational data stored in sql databases, csv files, and excel worksheets. 

### Long Description

SQL, Python, and Pandas are tools that can be used to parse, analyze, reformat, and generate insight into data. SQL is a particularly powerful declarative language that is very effective at querying and subsetting data from multiple tables with common colums. Pandas provides an extensive set of tools for building, sorting, subsetting, joining, and analyzing data in tabular format. Python provides all the flexibility of a full programming language, giving a programmer the ability to break apart and reassemble data with very few restrictions. 

Some tasks are most effectively approached with SQL, some are easier with Pandas dataframes, and some require using custom methods and user defined functions (often through a list-loop-conditional approach). Which tool to use may be a matter of personal preference and skill or comfort level. 

Instead of picking a single technolgoy that provies the "best" approach, it may be much more effective to approach a dataset by bringing in a combination of SQL statements, Pandas daframe operations, and user defined methods and programming constructs in Python.

This tutorial will consist of a series of notebooks and tutorial exercises. Rather than discussing SQL, Pandas, and Python methods in the abstract, participants will build a series of jupyter notebooks to:

1. Connect directly to a SQLite, write and execute a SQL query, parse results as Python tuples, and read results directly into a Pandas dataframe.

2. Load CSV files directly into a Pandas dataframe, and use dataframe operations such as merge to join tables on a common column.

3. Use the PandaSQL module to run SQL commands directly against a set of CSV files without the need to create a separate SQL database.

4. Use Pandas, Python, and SQL in concert to analze a dataset, with an emphasis on preparing and formatting data for analysis.

### Prerequisites

This tutorial will be most accessible for participants who have:

1. Basic programming experience with Python, including loops, conditionals, and lists. 

2. Basic Pandas dataframe operations, including indexing, selecting rows, selecting columns, filtering by value, and boolean masks. 

3. Basic SQL knowledge, including SELECT, WHERE, JOIN, GROUP BY, and HAVING statements.

#### Note: Plenty of people learn to program by picking something that looks interesting, tackling an intermediate or advanced topic, and backfilling their knowledge as they keep absorbing and learning. If you thrive on partially understanding things as you learn by doing, feel free to join the workshop without these prereqs. 

## Workbooks

This tutorial consists of a series of notebooks. Further explanatory text is available in the notebook for each section or the tutorial. 

### Python-SQLite

How to directly connect to a SQLite database using Python. How to query the data using SQL, and how to process results by rows of tuples. How to create views, modify data, and create new tables.

### Python-SQL-Pandas

How to read a query directly into a Pandas Dataframe. Advantages and disadvantages of this approach.

### PandaSQL

How to use the Pandasql module to work with CSV files and dataframes directly, without the intermediary step of creating a database.

### Case Studies

Using SQL, Pandas, Python, and PandaSQL to tackle some messy real world examples.

## Setup and Configuration

### Software

To follow along with the material in this workshop, you'll need to install:

DB Browser for SQLite
https://sqlitebrowser.org

Jupyter Notebook running Python 3.6 or later
https://www.anaconda.com/products/individual

Pandasql (to use sql directly on pandas dataframes, without a SQL database)
https://pypi.org/project/pandasql/

### Data

And you'll need to download the following files from https://figshare.com/articles/Portal_Project_Teaching_Database/1314459


1. species.csv
2. surveys.csv
3. plots.csv
4. portal_mammals.sqlite
