I am exploring the creation of a new EPICS record, to be called tableRecord. I have not settled on a desing, so first we need to do some research and then a couple of proof of concepts before settling on an implementation.

# Research

Research documentation on EPICS Records, qsrv and Normative Types (focused on NTTable) to get an understanding of the problem space. Then look at relevant records like aSub record and waveform record to learn about concrete implementation of existing records. This info is all available in the epics-base repository.

# Table Record Idea

The main idea for tableRecord is to operate in two main ways:

1. As a record with Soft Channel device support. In this mode, the record's resulting table is built based on data coming in from input fields / links.
2. As a record with a real device support. In this mode, the device support layer provides the record with the relevant data for the table to be built.

This record needs to be treated specially by qsrv. Meaning that qsrv should be able to publish the record over PVAccess as an NTTable value.

# Table Record possible designs

I am thinking of two possbile designs for the record:

## Design one: tableRecordA

With this design, the record will have the following fields:

COLNAMES - An array of strings specifying the column names for the table. Set at initialization and doesn't change for the lifetime of the record.
COLTYPES - An array of FTVL specifying the type of each column. Set at initialization and doesn't change for the lifetime of the record.
COLDATA - A byte array with the data for the columns. The data will be interpreted depending on COLTYPES. A big question mark here is how to encode the data from different columns into a this single field. Fixed size data should work fine, but string data is a question. Is qsrv capable of getting to this data reasonably?
NUMROWS - An integer specifying the number of rows in each column. The table is assumed to have the same number of rows in every column. This field could also be named NORD, similar to the waveform record.
MAXROWS - An integer specifying the maximum number of rows. Used for pre-allocating memory for the data. Similar to the NELM field in the waveform record.
COLCHD - An array (bitmask) indicating which rows have changed. This helps qsrv avoid serializing columns that haven't changed

## Design two: tableRecordB

With this design, the record will have the following fields:

COL00NAME - A string for the name of the first column, can be a link
COL00INP - An input link to populate the first column's data
COL00FTVL - An FTVL field to indicate the data type of this column.
COL00DATA - A byte array holding the column's data. It is not clear to me how to encode an array of variable-length strings here. Could maybe be named COL00VAL instead.
COL00CHGD - A boolean field to indicate if the data has changed since last processing. May not be needed.
COL00NORD - Number of elements in this column. Maybe this should not belong to the column, but should be a NORD field for all columns instead.
COL00NELM - Max number of elements in this column. Same consideration, maybe it should be a NELM field for all columns instead.

# Other considerations

NTTable can carry extra data, of any acceptable format. How to represent this in the record design?

# Final instructions

1. Perform the research as directed. Gain an understanding of EPICS and the problem space
2. Ask me any relevant questions
3. Create an implementation plan for the two proof of concept records in this repository (tableRecord). Make sure the plan is divided into small, well defined tasks that smaller models can implement.
4. Check how we can add support for this record in qsrv. Can we do it in this repository (tableRecord) or do we need to modify qsrv itself?
5. Based on your learnings from #4, add the implementation plan for the necessary change for qsrv to the overall plan. Again, divide it into small (parallelizable if possible) implementation tasks that smaller models can take up.
