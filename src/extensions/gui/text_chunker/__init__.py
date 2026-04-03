"""
Text Chunker Extension for Havel WM

Provides text splitting, chunking, and processing utilities.

Example:
    from havel.extensions.gui.text_chunker import TextChunkerExtension
    
    chunker = TextChunkerExtension()
    chunks = chunker.split_by_size(text, 1000)
    chunks = chunker.split_by_paragraph(text)
"""

from .TextChunkerWindow import TextChunkerWindow

__all__ = ['TextChunkerWindow', 'TextChunkerExtension']

class TextChunkerExtension:
    """Scriptable interface for text chunking."""
    
    def __init__(self):
        self._chunker = None
    
    def create(self, parent=None):
        """Create the text chunker window."""
        self._chunker = TextChunkerWindow(parent)
        return self._chunker
    
    def show(self):
        """Show the text chunker window."""
        if self._chunker:
            self._chunker.show()
        return self
    
    def hide(self):
        """Hide the text chunker."""
        if self._chunker:
            self._chunker.hide()
        return self
    
    def split_by_size(self, text, chunk_size, overlap=0):
        """Split text into chunks of specified size.
        
        Args:
            text: Input text to split
            chunk_size: Maximum size of each chunk
            overlap: Number of characters to overlap between chunks
        """
        if self._chunker:
            return self._chunker.splitBySize(text, chunk_size, overlap)
        return []
    
    def split_by_paragraph(self, text):
        """Split text by paragraphs."""
        if self._chunker:
            return self._chunker.splitByParagraph(text)
        return []
    
    def split_by_delimiter(self, text, delimiter):
        """Split text by a custom delimiter."""
        if self._chunker:
            return self._chunker.splitByDelimiter(text, delimiter)
        return []
    
    def process_chunks(self, chunks, processor):
        """Process each chunk with a custom function.
        
        Args:
            chunks: List of text chunks
            processor: Function to apply to each chunk
        """
        if self._chunker:
            return self._chunker.processChunks(chunks, processor)
        return []
    
    def merge_chunks(self, chunks, separator="\n\n"):
        """Merge chunks back into a single text."""
        if self._chunker:
            return self._chunker.mergeChunks(chunks, separator)
        return ""

def create_text_chunker(parent=None):
    """Create and return a TextChunkerWindow instance."""
    return TextChunkerExtension().create(parent)
